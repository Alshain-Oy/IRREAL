#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <stdint.h>
#include <map>
#include <vector>
#include <deque>
#include <string>
#include <iterator>
#include <pthread.h>

#define NUM_OF_THREADS 8

const std::string WHITESPACE( " \t\n\r" );
const std::string NUMBERS( "1234567890" );

pthread_mutex_t global_contexts_lock = PTHREAD_MUTEX_INITIALIZER, global_stacks_lock = PTHREAD_MUTEX_INITIALIZER, global_vm_queue_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t global_running_vms_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t global_running_vms = 0;

bool global_running_threads[ NUM_OF_THREADS ];
uint64_t global_running_threads_vm[ NUM_OF_THREADS ];


#define TYPE_OPERATOR 	128
#define TYPE_INTEGER 	2
#define TYPE_SYMBOL 	3
#define TYPE_STRING 	4
#define TYPE_SENTINEL	5

#define STATE_OK 		0
#define STATE_NOT_YET 	1
#define STATE_JOINING 	2
#define STATE_SYNCING 	3

#define CMD_BEGIN 	(0x80 | 1 )
#define CMD_END 	(0x80 | 2 )
#define CMD_PUSH	(0x80 | 3 )
#define CMD_POP		(0x80 | 4 )
#define CMD_DEF		(0x80 | 5 )
#define CMD_MERGE	(0x80 | 6 )
#define CMD_CALL	(0x80 | 7 )
#define CMD_JOIN	(0x80 | 8 )
#define CMD_ADD		(0x80 | 9 )
#define CMD_PRINT	(0x80 | 10 )
#define CMD_SYNC	(0x80 | 11 )
#define CMD_DUP		(0x80 | 12 )
#define CMD_WHILE	(0x80 | 13 )
#define CMD_IF		(0x80 | 14 )
#define CMD_SUB		(0x80 | 15 )
#define CMD_MUL		(0x80 | 16 )
#define CMD_DIV		(0x80 | 17 )
#define CMD_MOD		(0x80 | 18 )
#define CMD_LENGTH	(0x80 | 19 )
#define CMD_MACRO	(0x80 | 20 )
#define CMD_SWAP	(0x80 | 21 )
#define CMD_ROTR	(0x80 | 22 )
#define CMD_ROTL	(0x80 | 23 )



std::string debug_cmd_names[] = { "", "BEGIN", "END", "PUSH", "POP", "DEF", 
								"MERGE", "CALL", "JOIN", "ADD",  "PRINT",
								"SYNC", "DUP", "WHILE", "IF", "SUB", "MUL", "DIV", "MOD", "LENGTH", "MACRO", "SWAP", "ROTR", "ROTL" };

std::string integer_to_string( long int integer ){
	char buffer[64];
	sprintf( buffer, "%li", integer );
	return std::string( buffer );
	}

long int string_to_integer( std::string str ){
	return atol( str.c_str() );
	}

void test_for_error( bool is_null, std::string error ){
	if( is_null ){
		fprintf( stderr, "ERROR: %s\n", error.c_str() );
		exit( 1 );
		}
	}


class IrrealValue {
	public:
		IrrealValue();
		IrrealValue( uint8_t, uint8_t, std::string );
		void setType( uint8_t );
		uint8_t getType();
		void setState( uint8_t );
		uint8_t getState();
		void setValue( std::string );
		
		std::string getValue();
		
	private:
		uint8_t type, state;
		std::string value;
	};

IrrealValue :: IrrealValue(){ type = 0; state = STATE_OK; value = ""; }

IrrealValue :: IrrealValue( uint8_t aType, uint8_t aState, std::string aValue ){
	type = aType;
	state = aState;
	value = aValue;
	}

void IrrealValue :: setType( uint8_t aType ){ type = aType; }
uint8_t IrrealValue :: getType(){ return type; }

void IrrealValue :: setState( uint8_t aState ){ state = aState; }
uint8_t IrrealValue :: getState(){ return state; }

void IrrealValue :: setValue( std::string aValue ){ value = aValue; }

std::string IrrealValue :: getValue(){ return value; }

class IrrealStack {
	public:
		IrrealStack();
		void push( IrrealValue * );
		IrrealValue* pop();
		IrrealValue* peek();
		bool isJoined();
		size_t size();
		void merge( IrrealStack *, bool );
		void nondestructive_merge( IrrealStack *, bool );
		
		std::vector< IrrealValue* >* get_internals();
		
		void _debug_print();
		uint64_t _debug_get_counter();
		
		uint64_t get_id();
		
		void rotate_stack( bool );
		
	private:
		std::vector< IrrealValue* > stack;
		pthread_mutex_t stack_lock;
		uint64_t pop_counter;
		uint64_t stack_id;
		
		static uint64_t next_stack_id;
	
	};

uint64_t IrrealStack :: next_stack_id = 0;

IrrealStack :: IrrealStack(){
	pthread_mutex_init( &stack_lock, NULL );
	pop_counter = 0;
	stack_id = next_stack_id;
	++next_stack_id;
	
	stack.reserve( 64 );
	
	}

uint64_t IrrealStack :: get_id(){ return stack_id; }

void IrrealStack :: _debug_print(){
	for( size_t i = 0 ; i < stack.size() ; ++i ){
		if( stack[i]->getType() & TYPE_OPERATOR ){
			printf( "%s ", debug_cmd_names[ stack[i]->getType() & (~0x80 ) ].c_str() );
			}
		else{
			printf( "%s ", stack[i]->getValue().c_str() );
			}
		}
	printf( "\n" );
	}

uint64_t IrrealStack :: _debug_get_counter(){ return pop_counter; }

void IrrealStack :: push( IrrealValue *value ){
	
	pthread_mutex_lock( &stack_lock );
	
	stack.push_back( value );
	
	pthread_mutex_unlock( &stack_lock );
	}

IrrealValue* IrrealStack :: pop(){

	pthread_mutex_lock( &stack_lock );
	
	++pop_counter;
	
	if( stack.size() < 1 ){
		
		pthread_mutex_unlock( &stack_lock );
		return NULL; 
		}
	IrrealValue* out = stack[stack.size()-1];
	stack.pop_back();
	
	pthread_mutex_unlock( &stack_lock );
	return out;
	}

IrrealValue* IrrealStack :: peek(){

	pthread_mutex_lock( &stack_lock );
	
	if( stack.size() < 1 ){
		pthread_mutex_unlock( &stack_lock );
		return NULL; 
		}
	IrrealValue* out = stack[stack.size()-1];
	pthread_mutex_unlock( &stack_lock );
	return out;
	}

// Check if there is any not ready values in the stack
bool IrrealStack :: isJoined(){
	pthread_mutex_lock( &stack_lock );
	
	for( size_t i = 0 ; i < stack.size() ; ++i ){
		if( stack[i]->getState() == STATE_NOT_YET ){
			pthread_mutex_unlock( &stack_lock );
			return false; 
			}
		}
		
	pthread_mutex_unlock( &stack_lock );
	return true;
	}

size_t IrrealStack :: size(){
	size_t out;
	
	pthread_mutex_lock( &stack_lock );
	out = stack.size();
	pthread_mutex_unlock( &stack_lock );
	
	return out; 
	}

std::vector< IrrealValue* >* IrrealStack :: get_internals(){
	return &stack;
	}

void IrrealStack :: nondestructive_merge( IrrealStack *other, bool reverse ){
	
	std::vector< IrrealValue* > *other_stack = other->get_internals();
	pthread_mutex_lock( &stack_lock );
	
	if( reverse ){
		for( size_t i = 0 ; i < other_stack->size() ; ++i ){
			stack.push_back( other_stack->at( i ) );
			}
		}
	else{
		for( size_t i = 0 ; i < other_stack->size() ; ++i ){
			stack.push_back( other_stack->at( other_stack->size() - i - 1 ) );
			}
		
		}
	
	pthread_mutex_unlock( &stack_lock );
	}

void IrrealStack :: merge( IrrealStack *other, bool reverse ){
	IrrealValue *tmp;
	tmp = other->pop();
	
	std::vector< IrrealValue* > tmp_stack;
	
	pthread_mutex_lock( &stack_lock );
	
	if( reverse ){
	
		while( tmp != NULL ){
			stack.push_back( tmp );
			tmp = other->pop();
			}
	
		}
	else{
		
		while( tmp != NULL ){
			tmp_stack.push_back( tmp );
			tmp = other->pop();
			}
		
		for( size_t i = 0 ; i < tmp_stack.size() ; ++i ){
			stack.push_back( tmp_stack[i] );
			}
		
		}
	
	pthread_mutex_unlock( &stack_lock );
	}

void IrrealStack :: rotate_stack( bool dir ){
	/*
	if( dir ){
		IrrealValue 
		
		
		}
	*/
	}

std::map< std::string, IrrealStack > global_stacks;


class IrrealContext {
	public:
		IrrealContext();
		IrrealStack* getCurrentStack();
		IrrealStack* getCodeStack();
		void spawnNewStack( std::string );
		std::string spawnNewAnonymousStack();

		IrrealStack* getStack( std::string );
		std::vector< std::string > getScope();
		void pushScope( std::string );
		void mergeScope( std::vector<std::string> );
		
		uint8_t getState();
		void setState( uint8_t );
		
		void setReturnValue( IrrealValue * );
		IrrealValue* getReturnValue();
		std::string getOutStackName();
		
		uint64_t get_id();
	
		void lock_context();
		void unlock_context();
	
		void mark();
		uint64_t read_marks();
	
	private:
		std::string prefix;
		std::vector<std::string> scope;
		std::vector<std::string> spawned_stacks;
		uint8_t state;
		IrrealValue *return_value;
		
		pthread_mutex_t context_lock;
		
		uint64_t context_id, marks;
		
		
		static uint64_t next_context_id;
		static uint64_t next_anon_stack_id;
	};

uint64_t IrrealContext :: next_context_id = 0;
uint64_t IrrealContext :: next_anon_stack_id = 0;

std::map< uint64_t, IrrealContext* > global_contexts;

IrrealContext :: IrrealContext(){
	
	pthread_mutex_lock( &global_contexts_lock );
	
	context_id = next_context_id;
	prefix = integer_to_string( context_id ) + std::string( "::" );
	
	global_contexts[ context_id ] = this;

	++next_context_id;
	
	pthread_mutex_lock( &global_stacks_lock );
	global_stacks[ prefix + std::string( "CURRENT" ) ] = IrrealStack(); 
	global_stacks[ prefix + std::string( "PARAMS" ) ] = IrrealStack(); 
	global_stacks[ prefix + std::string( "CODE" ) ] = IrrealStack(); 
	global_stacks[ prefix + std::string( "OUT" ) ] = IrrealStack(); 
	pthread_mutex_unlock( &global_stacks_lock );
	
	scope.push_back( prefix );
	
	state = STATE_OK;
	
	
	return_value = NULL;
	
	//context_lock = PTHREAD_MUTEX_INITIALIZER;
	
	pthread_mutex_init( &context_lock, NULL );
	
	marks = 0;
	
	pthread_mutex_unlock( &global_contexts_lock );
	
	}

uint8_t IrrealContext :: getState(){ return state; }
void IrrealContext :: setState( uint8_t new_state ){ state = new_state; }

void IrrealContext :: lock_context(){ pthread_mutex_lock( &context_lock ); }
void IrrealContext :: unlock_context(){ pthread_mutex_unlock( &context_lock ); }

void IrrealContext :: mark(){ ++marks; }
uint64_t IrrealContext :: read_marks(){ return marks; }

IrrealStack* IrrealContext :: getCurrentStack(){
	
	IrrealStack* out;
	
	pthread_mutex_lock( &global_stacks_lock );
	out = &global_stacks[ prefix + std::string( "CURRENT" ) ];
	pthread_mutex_unlock( &global_stacks_lock );
	
	return out;
	}

IrrealStack* IrrealContext :: getCodeStack(){
	
	IrrealStack* out;
	
	pthread_mutex_lock( &global_stacks_lock );
	out = &global_stacks[ prefix + std::string( "CODE" ) ];
	pthread_mutex_unlock( &global_stacks_lock );
	
	return out;
	}

void IrrealContext :: spawnNewStack( std::string name ){
	
	pthread_mutex_lock( &global_stacks_lock );
	global_stacks[ prefix + name ] = IrrealStack();
	pthread_mutex_unlock( &global_stacks_lock );
	
	
	spawned_stacks.push_back( name );
	}

std::string IrrealContext :: spawnNewAnonymousStack(){
	
	pthread_mutex_lock( &global_stacks_lock );
	
	std::string name = std::string( "_anon_" ) + integer_to_string( next_anon_stack_id );
	++next_anon_stack_id;
	global_stacks[ prefix + name ] = IrrealStack();
	
	pthread_mutex_unlock( &global_stacks_lock );
	
	spawned_stacks.push_back( name );
	
	return name;
	}

IrrealStack* IrrealContext :: getStack( std::string name ){
	
	IrrealStack *out;
	
	pthread_mutex_lock( &global_stacks_lock );
	
	for( size_t i = 0 ; i < scope.size() ; ++i ){
		std::string stack_name = scope[i] + name;
		std::map<std::string, IrrealStack>::iterator it = global_stacks.find( stack_name );
		if( it != global_stacks.end() ){
			out = &(it->second);
			pthread_mutex_unlock( &global_stacks_lock );
			return out;
			}
		}
	
	pthread_mutex_unlock( &global_stacks_lock );
	return NULL;
	}

std::vector< std::string > IrrealContext :: getScope(){ return scope; }

void IrrealContext :: pushScope( std::string level ){
	scope.push_back( level );
	}

void IrrealContext :: mergeScope( std::vector<std::string> levels ){
	for( long int i = levels.size() - 1 ; i >= 0 ; --i ){
		scope.push_back( levels[i] );
		}
	}

void IrrealContext :: setReturnValue( IrrealValue *value ){
	return_value = value;
	}
IrrealValue* IrrealContext :: getReturnValue(){ return return_value; }

uint64_t IrrealContext :: get_id(){ return context_id;  }

std::string IrrealContext :: getOutStackName(){
	return prefix + std::string( "OUT" );
	}

std::deque< uint64_t > global_vm_queue;

class IrrealVM {
	public:
		static void execute( uint64_t );
	};


void _debug_running_threads(){
	printf( "Running threads: " );
	for( size_t i = 0 ; i < NUM_OF_THREADS ; ++i ){
		
		if( global_running_threads[i] ){
			printf( "%lu ", global_running_threads_vm[i] );
			}
		else{
			printf( "_ " );
			}
		
		}
	printf( "\n" );
	
	}

void IrrealVM :: execute( uint64_t thread_id ){
	
	//if( global_vm_queue.size() < 1 ){ return; }
	
	pthread_mutex_lock( &global_vm_queue_lock );
	
	if( global_vm_queue.size() < 1 ){
		pthread_mutex_unlock( &global_vm_queue_lock );
		return; 
		}
	
	uint64_t ctx_id = global_vm_queue.front();
	global_vm_queue.pop_front();
	
	IrrealContext *ctx = global_contexts[ ctx_id ];
	
	pthread_mutex_unlock( &global_vm_queue_lock );
	
	test_for_error( ctx == NULL, "Invalid context!" );
	
	ctx->lock_context();
	
	global_running_threads_vm[ thread_id ] = ctx_id;
	
	//printf( "Executing vm #%lu\n", ctx_id );
	//_debug_running_threads();
	
	
	IrrealStack *current, *code;
	
	current = ctx->getCurrentStack();
	code = ctx->getCodeStack();
	
	test_for_error( current == NULL, "Invalid current stack!" );
	test_for_error( code == NULL, "Invalid code stack!" );
	
	uint8_t state = ctx->getState();
	
	switch( state ){
		case STATE_JOINING:
			if( !current->isJoined() ){
				
				pthread_mutex_lock( &global_vm_queue_lock );
				global_vm_queue.push_back( ctx_id ); 
				pthread_mutex_unlock( &global_vm_queue_lock );
	
				ctx->mark();
	
				ctx->unlock_context();
				return;
				}
		break;
		case STATE_SYNCING:
			
			test_for_error( current->peek() == NULL, "Not enough values to perform 'sync'!" );
			
			//printf( "syncing... ('%s')\n", current->peek()->getValue().c_str() );
			if( current->peek()->getState() == STATE_NOT_YET ){
				
				pthread_mutex_lock( &global_vm_queue_lock );
				global_vm_queue.push_back( ctx_id ); 
				pthread_mutex_unlock( &global_vm_queue_lock );
				
				ctx->mark();
				
				ctx->unlock_context();
				return;
				}
			//printf( "Data synced, now continuing... \n" );
		break;
		}
	
	std::string anon_name;
	bool anon_state = false;
	uint64_t begin_end_counter = 0;
	bool done = false;
	IrrealStack *anon_stack = NULL;
	
	long int debug_value;
	
	IrrealStack *debug_stack;
	
	debug_value = ctx->getStack("PARAMS")->size();
	debug_stack = ctx->getStack("PARAMS");
	while( !done ){
		ctx->mark();
		//printf( "\n\n" ); 
		IrrealValue *q = code->pop();
		//printf( "current: " ); current->_debug_print();
		//printf( "params: " ); ctx->getStack("PARAMS")->_debug_print();
		//printf( "out: " ); ctx->getStack("OUT")->_debug_print();
		if( q == NULL ){
			//printf( "q == NULL\n" );
			done = true; 
			if( ctx->getReturnValue() != NULL ){
				//printf( "Returning value! ('%s')\n", ctx->getReturnValue()->getValue().c_str() );
				ctx->getStack( ctx->getReturnValue()->getValue() )->merge( ctx->getStack( "OUT" ), false ); 
				
				ctx->getReturnValue()->setType( TYPE_SYMBOL );
				ctx->getReturnValue()->setState( STATE_OK );
				}
			
			pthread_mutex_lock( &global_running_vms_lock );
				--global_running_vms;
			pthread_mutex_unlock( &global_running_vms_lock );
			
			continue; 
			}
		if( q->getType() & TYPE_OPERATOR ){
			printf( "q = {'%s', %s} \n", q->getValue().c_str(), debug_cmd_names[ q->getType() & (~0x80 ) ].c_str() );
			}
		else{
			printf( "q = {'%s', %i} \n", q->getValue().c_str(), q->getType() );
			}
		if( anon_state ){
			if( q->getType() & TYPE_OPERATOR ){
				switch( q->getType() ){
					case CMD_BEGIN:
						++begin_end_counter;
					break;
					case CMD_END:
						--begin_end_counter;
					break;
					}
				anon_stack->push( q );
				if( begin_end_counter == 0 ){
					 test_for_error( anon_stack == NULL, "Stack error when parsing block!" );
					 anon_stack->pop();
					 
					 anon_state = false;
					 IrrealValue* value = new IrrealValue();
					 value->setType( TYPE_SYMBOL );
					 value->setValue( anon_name );
					 current->push( value );
					 //printf( "pushing to stack: '%s' \n", current->peek()->getValue().c_str() );
					 continue;
					 }
				}
			else{
				test_for_error( anon_stack == NULL, "Stack error when parsing block!" );
				anon_stack->push( q );
				}
			}
		else{
			
			if( q->getType() & TYPE_OPERATOR ){
				//printf( "Executing command: %s \n", debug_cmd_names[ q->getType() & (~0x80) ].c_str() );
				switch( q->getType() ){
					
					case CMD_BEGIN:
					{
						anon_name = ctx->spawnNewAnonymousStack();
						anon_stack = ctx->getStack( anon_name );
						test_for_error( anon_stack == NULL, "Unable to spawn new anonymous stack!" );
						
						begin_end_counter = 1;
						anon_state = true;
						
					}
					break;
					
					case CMD_PUSH:
					{
						IrrealValue *target_stack_name;
						IrrealValue *value;
						IrrealStack *target_stack;
						
						target_stack_name = current->pop();
						
						value = current->pop();
						
						test_for_error( target_stack_name == NULL, "Not enough values to perform 'push'!" );
						test_for_error( value == NULL, "Not enough values to perform 'push'!" );
						
						
						target_stack = ctx->getStack( target_stack_name->getValue() );
						
						test_for_error( target_stack == NULL, "PUSH: Stack not found!" );
						
						target_stack->push( value );
					}
					break;
					
					case CMD_POP:
					{	
						IrrealValue *target_stack_name;
						IrrealStack *target_stack, *testing;
						IrrealValue *value;
						
						target_stack_name = current->pop();
						
						test_for_error( target_stack_name == NULL, "Not enough values to perform 'pop'!" );
						
						target_stack = ctx->getStack( target_stack_name->getValue() );
						testing = ctx->getStack( target_stack_name->getValue() );
							
						test_for_error( target_stack == NULL, "POP: Stack not found!" );
						
						value = target_stack->pop();
						
						
						if( value == NULL ){
							printf( "\n\n**** Debug info***\n\n" );
							printf( "In PARAMS stack there were %li entries in the beginning...\n", debug_value ); 
							printf( "target_stack_name = '%s' \n", target_stack_name->getValue().c_str() );
							printf( "Context mark count: %lu \n", ctx->read_marks() );
							printf( "target_stack pop_counter = %lu \n", target_stack->_debug_get_counter() );
							printf( "target_stack = %p, target_stack->id = %lu \n", target_stack, target_stack->get_id() );
							printf( "debug_stack = %p, debug_stack->id = %lu \n", debug_stack, debug_stack->get_id() );
							printf( "testing: %p, testing->id = %lu\n", testing, testing->get_id() );
							printf( "debug_stack->size = %lu, target_stack->size = %lu, testing->size = %lu \n", debug_stack->size(), target_stack->size(), testing->size() );
							value = testing->pop();
							printf( "testing->pop = %p, value->getValue = %s\n", value, value->getValue().c_str() ); 
							printf( "\n" );
							fflush( stdout );
							value = NULL;
							}
						test_for_error( value == NULL, "POP: Target stack empty!" );
						
						current->push( value );
					}
					break;
					
					case CMD_DEF:
					{	
						IrrealValue *target_name;
						IrrealValue *value;
						
						target_name = current->pop();
						value = current->pop();
						
						test_for_error( target_name == NULL, "Not enough values to perform 'def'!" );
						test_for_error( value == NULL, "No enough values to perform 'def'!" );
						
						ctx->spawnNewStack( target_name->getValue() );
						
						switch( value->getType() ){
							case TYPE_SYMBOL:
							{
								IrrealStack *target_stack = ctx->getStack( target_name->getValue() );
								IrrealStack *source_stack = ctx->getStack( value->getValue() );
								
								test_for_error( target_stack == NULL, "DEF: Target stack not found!" );
								test_for_error( source_stack == NULL, "DEF: Source stack not found!" );
								
								
								IrrealValue *tmp = source_stack->pop();
								
								
								while( tmp != NULL ){
									target_stack->push( tmp );
									tmp = source_stack->pop();
									}
							}
							break;
							default:
							{
								IrrealStack *target_stack = ctx->getStack( target_name->getValue() );
								test_for_error( target_stack == NULL, "DEF: Target stack not found!" );
								target_stack->push( value );
							}	
							break;
							}
					}	
					break;
					
					case CMD_MERGE:
					{
						IrrealValue *target_name;
						IrrealStack *target_stack;
						
						target_name = current->pop();
						test_for_error( target_name == NULL, "Not enough values to perform 'merge'!" );
						
						target_stack = ctx->getStack( target_name->getValue() );
						
						test_for_error( target_stack == NULL, "MERGE: Stack not found!" );
						
						current->merge( target_stack, false );
					}	
					break;
					
					case CMD_CALL:
					{
						IrrealValue *func, *nparams, *return_value;
						
						nparams = current->pop();
						func = current->pop();
						
						test_for_error( nparams == NULL, "Not enough values to perform 'call'!" );
						test_for_error( func == NULL, "Not enough values to perform 'call'!" );
						
						
						IrrealContext *new_ctx = new IrrealContext();
						
						new_ctx->lock_context();
						
						return_value = new IrrealValue();
						return_value->setType( TYPE_SENTINEL );
						return_value->setState( STATE_NOT_YET );
						return_value->setValue( ctx->spawnNewAnonymousStack() );
						
						//printf( "current->peek() = '%s' \n", current->peek()->getValue().c_str() );
						
						new_ctx->setReturnValue( return_value );
						
						IrrealStack *func_stack = ctx->getStack( func->getValue() );
						
						test_for_error( func_stack == NULL, "CALL: Function not found!" );
						
						new_ctx->getCodeStack()->nondestructive_merge( func_stack, true );
						
						size_t N = string_to_integer( nparams->getValue() );
						
						//printf( "nparams: %lu \n", N );
						
						IrrealStack *params = new_ctx->getStack( "PARAMS" );
						for( size_t i = 0 ; i < N ; ++i ){
							IrrealValue *p = current->pop();
							test_for_error( p == NULL, "Not enough values to perform 'call'!" );
							if( p->getType() == TYPE_SYMBOL ){
								std::string stack_name = ctx->spawnNewAnonymousStack();
								IrrealStack *pstack = ctx->getStack( stack_name );
								IrrealStack *target_stack = ctx->getStack( p->getValue() );
								
								test_for_error( pstack == NULL, "CALL: Unable to spawn new anonymous stack!" );
								test_for_error( target_stack == NULL, "CALL: Undefined symbol!" );
								
								pstack->nondestructive_merge( target_stack, false );
								params->push( new IrrealValue( TYPE_SYMBOL, STATE_OK, stack_name ) );
								}
							else{
								params->push( p );
								}
								
							}
						//printf( "Calling with params: "); params->_debug_print();
						
						//printf( "Merging scope...\n" );
						
						new_ctx->mergeScope( ctx->getScope() );
						
						new_ctx->unlock_context();
						
						pthread_mutex_lock( &global_vm_queue_lock );
						global_vm_queue.push_front( new_ctx->get_id() );
						pthread_mutex_unlock( &global_vm_queue_lock );
						
						pthread_mutex_lock( &global_running_vms_lock );
							++global_running_vms;
						pthread_mutex_unlock( &global_running_vms_lock );
						
						
						current->push( return_value );
						//printf( "current->peek() = '%s' \n", current->peek()->getValue().c_str() );
						
					}
					break;
					
					case CMD_JOIN:
						ctx->setState( STATE_JOINING );
						done = true;
						pthread_mutex_lock( &global_vm_queue_lock );
						global_vm_queue.push_back( ctx->get_id() );
						pthread_mutex_unlock( &global_vm_queue_lock );
						
					break;
					
					case CMD_ADD:
					{
						IrrealValue *first, *second, *value;
						first = current->pop();
						second = current->pop();
						
						test_for_error( first == NULL, "Not enough values to perform 'add'!" );
						test_for_error( second == NULL, "Not enough values to perform 'add'!" );
						
						
						value = new IrrealValue();
						value->setType( TYPE_INTEGER );
						value->setValue( integer_to_string( string_to_integer( first->getValue() ) + string_to_integer( second->getValue() ) ) );
						
						current->push( value );
					}
					break;
					
					case CMD_PRINT:
					{
						IrrealValue *value;
						value = current->pop();
						test_for_error( value == NULL, "Not enough values to perform 'print'!" );
						printf( "print: type = %i, state = %i, value = '%s' \n", value->getType(), value->getState(), value->getValue().c_str() );
						
					}
					break;
					
					case CMD_SYNC:
						ctx->setState( STATE_SYNCING );
						done = true;
						pthread_mutex_lock( &global_vm_queue_lock );
						global_vm_queue.push_back( ctx->get_id() );
						pthread_mutex_unlock( &global_vm_queue_lock );
					break;
					
					case CMD_DUP:
					{
						IrrealValue *value, *new_value;
						value = current->pop();
						test_for_error( value == NULL, "Not enough values to perform 'dup'!" );
						new_value = new IrrealValue( value->getType(), value->getState(), value->getValue() );
					
						current->push( value );
						current->push( new_value );
					}
					break;

					case CMD_WHILE:
					{
						IrrealValue *test, *body;
						
						
/*
{...} {some tests} while

some tests
{ ... 
* {...} {some tests} while 
} {} if

*/
						test = current->pop();
						body = current->pop();
						
						test_for_error( test == NULL, "Not enough values to perform 'while'!" );
						test_for_error( body == NULL, "Not enough values to perform 'while'!" );
						
						
						IrrealStack *new_code = new IrrealStack();
						IrrealStack *test_stack = ctx->getStack( test->getValue() );
						IrrealStack *body_stack = ctx->getStack( body->getValue() );
						
						test_for_error( test_stack == NULL, "Invalid test stack for 'while'!" );
						test_for_error( body_stack == NULL, "Invalid body stack for 'while'!" );
						
						new_code->nondestructive_merge( test_stack, true );
						 						
						new_code->push( new IrrealValue( CMD_BEGIN, STATE_OK, "" ) );
						new_code->nondestructive_merge( body_stack, true );
						new_code->push( body );
						new_code->push( test );
						new_code->push( new IrrealValue( CMD_WHILE, STATE_OK, "" ) );
						new_code->push( new IrrealValue( CMD_END, STATE_OK, "" ) );
						new_code->push( new IrrealValue( CMD_BEGIN, STATE_OK, "" ) );
						new_code->push( new IrrealValue( CMD_END, STATE_OK, "" ) );
						new_code->push( new IrrealValue( CMD_IF, STATE_OK, "" ) );
						
						
						
						//printf( "while: new_code: " ); new_code->_debug_print();
						
						code->merge( new_code, false );
						
						delete new_code;
					}
					break;
					
					case CMD_IF:
					{
						IrrealValue *test, *block_true, *block_false;
							
						block_false = current->pop();
						block_true = current->pop();
						test = current->pop();
						
						test_for_error( block_false ==  NULL, "Not enough values to perform 'if'!" );
						test_for_error( block_true ==  NULL, "Not enough values to perform 'if'!" );
						test_for_error( test ==  NULL, "Not enough values to perform 'if'!" );
						
						
						IrrealStack *stack_true, *stack_false;
						
						stack_true = ctx->getStack( block_true->getValue() );
						stack_false = ctx->getStack( block_false->getValue() );
						
						test_for_error( stack_true == NULL, "IF: Stack (true) not found!" );
						test_for_error( stack_false == NULL, "IF: Stack (false) not found!" );
						
						
						//printf( "if: stack_true: " ); stack_true->_debug_print();
						//printf( "if: stack_false: " ); stack_false->_debug_print();
						
						//if( test == NULL ){ printf( "if: test: null!\n" ); } 
						//printf( "if: test value: %li \n", string_to_integer( test->getValue() ) );
						
						if( string_to_integer( test->getValue() ) ){
							
							code->nondestructive_merge( stack_true, false );
							}
						else{
							code->nondestructive_merge( stack_false, false );
							}
						
					}
					break;
					
					case CMD_SUB:
					{
						IrrealValue *first, *second, *value;
						second = current->pop();
						first = current->pop();
						
						test_for_error( first == NULL, "Not enough values to perform 'sub'!" );
						test_for_error( second == NULL, "Not enough values to perform 'sub'!" );
						
						
						value = new IrrealValue();
						value->setType( TYPE_INTEGER );
						value->setValue( integer_to_string( string_to_integer( first->getValue() ) - string_to_integer( second->getValue() ) ) );
						
						current->push( value );
					}
					break;

					case CMD_MUL:
					{
						IrrealValue *first, *second, *value;
						first = current->pop();
						second = current->pop();
						
						test_for_error( first == NULL, "Not enough values to perform 'mul'!" );
						test_for_error( second == NULL, "Not enough values to perform 'mul'!" );
						
						
						value = new IrrealValue();
						value->setType( TYPE_INTEGER );
						value->setValue( integer_to_string( string_to_integer( first->getValue() ) * string_to_integer( second->getValue() ) ) );
						
						current->push( value );
					}
					break;

					case CMD_DIV:
					{
						IrrealValue *first, *second, *value;
						second = current->pop();
						first = current->pop();
						
						test_for_error( first == NULL, "Not enough values to perform 'div'!" );
						test_for_error( second == NULL, "Not enough values to perform 'div'!" );
						
						
						value = new IrrealValue();
						value->setType( TYPE_INTEGER );
						value->setValue( integer_to_string( string_to_integer( first->getValue() ) / string_to_integer( second->getValue() ) ) );
						
						current->push( value );
					}
					break;

					case CMD_MOD:
					{
						IrrealValue *first, *second, *value;
						second = current->pop();
						first = current->pop();
						
						test_for_error( first == NULL, "Not enough values to perform 'mod'!" );
						test_for_error( second == NULL, "Not enough values to perform 'mod'!" );
						
						value = new IrrealValue();
						value->setType( TYPE_INTEGER );
						value->setValue( integer_to_string( string_to_integer( first->getValue() ) % string_to_integer( second->getValue() ) ) );
						
						current->push( value );
					}
					break;

					case CMD_LENGTH:
					{
						IrrealValue *value;
						value = current->pop();
						
						test_for_error( value == NULL, "Not enough values to perform 'length'!" );
						
						current->push( new IrrealValue( TYPE_INTEGER, STATE_OK, integer_to_string( ctx->getStack( value->getValue() )->size() ) ) );
					
					}
					break;
					
					case CMD_MACRO:
					{
						IrrealValue *value;
						value = current->pop();
						
						test_for_error( value == NULL, "Not enough values to perform 'macro'!" );
						
						//printf( "MACRO: debug: stack name = '%s'\n", value->getValue().c_str() );
						
						IrrealStack *source_stack = ctx->getStack( value->getValue() );
						
						test_for_error( source_stack == NULL, "MACRO: Invalid source stack!" );
						
						code->nondestructive_merge( source_stack, true );
						
					}
					break;
					
					case CMD_SWAP:
					{
						IrrealValue *stack_name, *value0, *value1;
						IrrealStack *target_stack;
						
						stack_name = current->pop();
						
						test_for_error( stack_name == NULL, "Not enough values to perform 'swap'!" );
						
						target_stack = ctx->getStack( stack_name->getValue() );
						
						test_for_error( target_stack == NULL, "SWAP: Invalid stack!" );
						
						value0 = target_stack->pop();
						value1 = target_stack->pop();
						
						test_for_error( value0 == NULL, "SWAP: Not enough values in target stack!" );
						test_for_error( value1 == NULL, "SWAP: Not enough values in target stack!" );
						
						target_stack->push( value0 );
						target_stack->push( value1 );
						
					
					}
					break;
					
					case CMD_ROTR:
					{
						
					
					}
					break;
					
					default:
					break;
					}
				}
			else{
				current->push( q );
				}
			}
		
		}
	
	
	ctx->unlock_context();
	}


std::string trim( const std::string str ){
	size_t start, stop;
	start = str.find_first_not_of( WHITESPACE );
	stop = str.find_last_not_of( WHITESPACE );
	return str.substr( start, stop-start+1 );
	} 

std::vector< std::string > split_string( const std::string input_str ){
	std::vector<std::string> out;

	std::string tmp;
	for( size_t i = 0 ; i < input_str.size() ; ++i ){
		if( input_str[i] == ' ' || input_str[i] == '\t' || input_str[i] == '\n' ){
			if( tmp.size() > 0 ){ out.push_back( tmp ); tmp = std::string(); }
			}
		else{
			tmp += input_str[i];
			}
		}
	if( tmp.size() > 0 ){ out.push_back( tmp ); }
	
	return out;
	
	}

void print_lines( std::vector< std::string > lines ){
	for( size_t i = 0 ; i < lines.size() ; ++i ){
		printf( "%lu: '%s'\n", i, lines[i].c_str() );
		}
	} 

IrrealValue* extract_value( std::string str ){
	if( str.find_first_not_of( NUMBERS ) == std::string::npos ){
		return new IrrealValue( TYPE_INTEGER, STATE_OK, str );
		}
	if( str == "{" ){ return new IrrealValue( CMD_BEGIN, STATE_OK, "" ); }
	if( str == "}" ){ return new IrrealValue( CMD_END, STATE_OK, "" ); }
	
	if( str == "push" ){ return new IrrealValue( CMD_PUSH, STATE_OK, "" ); }
	if( str == "pop" ){ return new IrrealValue( CMD_POP, STATE_OK, "" ); }
	if( str == "def" ){ return new IrrealValue( CMD_DEF, STATE_OK, "" ); }
	if( str == "merge" ){ return new IrrealValue( CMD_MERGE, STATE_OK, "" ); }
	if( str == "call" ){ return new IrrealValue( CMD_CALL, STATE_OK, "" ); }
	if( str == "join" ){ return new IrrealValue( CMD_JOIN, STATE_OK, "" ); }
	if( str == "add" ){ return new IrrealValue( CMD_ADD, STATE_OK, "" ); }
	if( str == "print" ){ return new IrrealValue( CMD_PRINT, STATE_OK, "" ); }
	if( str == "sync" ){ return new IrrealValue( CMD_SYNC, STATE_OK, "" ); }
	if( str == "while" ){ return new IrrealValue( CMD_WHILE, STATE_OK, "" ); }
	if( str == "if" ){ return new IrrealValue( CMD_IF, STATE_OK, "" ); }
	if( str == "sub" ){ return new IrrealValue( CMD_SUB, STATE_OK, "" ); }
	if( str == "mul" ){ return new IrrealValue( CMD_MUL, STATE_OK, "" ); }
	if( str == "div" ){ return new IrrealValue( CMD_DIV, STATE_OK, "" ); }
	if( str == "mod" ){ return new IrrealValue( CMD_MOD, STATE_OK, "" ); }
	if( str == "length" ){ return new IrrealValue( CMD_LENGTH, STATE_OK, "" ); }
	if( str == "dup" ){ return new IrrealValue( CMD_DUP, STATE_OK, "" ); }
	if( str == "macro" ){ return new IrrealValue( CMD_MACRO, STATE_OK, "" ); }
	if( str == "swap" ){ return new IrrealValue( CMD_SWAP, STATE_OK, "" ); }
	if( str == "rotl" ){ return new IrrealValue( CMD_ROTL, STATE_OK, "" ); }
	if( str == "rotr" ){ return new IrrealValue( CMD_ROTR, STATE_OK, "" ); }
		
	return new IrrealValue( TYPE_SYMBOL, STATE_OK, str );
	}

void *worker_thread( void *args ){
	size_t size, thread_id = (size_t)args;
	
	pthread_mutex_lock( &global_running_vms_lock );
	size = global_running_vms;
	pthread_mutex_unlock( &global_running_vms_lock );
	
	
	while( size > 0 ){
		
		global_running_threads[ thread_id ] = true;
		
		IrrealVM::execute( thread_id );
		
		global_running_threads[ thread_id ] = false;
		
		pthread_mutex_lock( &global_running_vms_lock);
		size = global_running_vms;
		//printf( "global_running_vms: %lu, queue size: %lu \n",global_running_vms, global_vm_queue.size() );
		pthread_mutex_unlock( &global_running_vms_lock);
		
		}
		
	pthread_exit( NULL );
	}

void init_threading(){
	
	for( size_t i = 0 ; i < NUM_OF_THREADS ; ++i ){
		global_running_threads[ i ] = false;
		}
	
	}

std::string read_file( const char *fn ){
	FILE *handle = fopen( fn, "rb" );
	char *buffer;
	std::string out;
	int n;
	fseek( handle, 0, SEEK_END );
	long int size = ftell( handle );
	rewind( handle );
	buffer = (char *)malloc( size + 1 );
	memset( buffer, 0, size + 1 );
	n = fread( buffer, 1, size, handle ); 
	fclose( handle );
	if( n < 0 ){ fprintf( stderr, "Error reading file '%s' \n", fn ); }
	out = std::string( buffer );
	free( buffer );
	return out;
	}

int main( int argc, char **argv ){


	IrrealContext context;
	IrrealStack code;

	if( argc < 2 ){
		fprintf( stderr, "Usage: %s file\n\n", argv[0] );
		return 1;
		}

	init_threading();
	
	std::string text = read_file( argv[1] );
	//printf( "'%s'\n", text.c_str() );
	std::vector<std::string> tokens = split_string( text );
	for( size_t i = 0 ; i < tokens.size() ; ++i ){
		//printf( "%s\n", tokens[i].c_str() );
		code.push( extract_value( tokens[i] ) );
		}
	
	//code._debug_print();
	
	context.getCodeStack()->merge( &code, true );
	global_vm_queue.push_front( context.get_id() );
	
	++global_running_vms;
	
	pthread_t workers[ NUM_OF_THREADS ];
	pthread_attr_t attr;
	
	void *status;
	
	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_JOINABLE );
	
	for( size_t i = 0 ; i < NUM_OF_THREADS ; ++i ){
		pthread_create( &workers[i], &attr, worker_thread, (void *)i );
		}
	
	pthread_attr_destroy( &attr );
	
	for( size_t i = 0 ; i < NUM_OF_THREADS ; ++i ){
		pthread_join( workers[i], &status ); 
		}
	pthread_exit( NULL );
	return 0;
	}
