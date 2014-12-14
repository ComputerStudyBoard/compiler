
#include "semantics.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "run_test.h"
#include "exprblock.h"
#include "exprop.h"
#include "exprfndef.h"

const char** g_pp,*g_p;
const char* g_filename=0;

int g_debug_get_instance=false;

struct VTablePtrs {
	void* expr_op;
	void* expr_ident;
	void* expr_fn_def;
	void* expr_block;
	void* type;
}
g_vtable_ptrs;
Node* g_pRoot;
bool g_verbose=true;
void lazy_cache_vtable_ptrs(){
	if (g_vtable_ptrs.expr_op)
		return;
	auto p1=new ExprOp(0,SrcPos{},0,0);
	g_vtable_ptrs.expr_op=*(void**)p1;
	
	auto p2=new ExprIdent();
	g_vtable_ptrs.expr_ident=*(void**)p2;
	
	auto p3=new ExprFnDef();
	g_vtable_ptrs.expr_fn_def=*(void**)p3;
	
	auto p4=new ExprBlock();
	g_vtable_ptrs.expr_block=*(void**)p4;
	
	auto p5=new Type();
	g_vtable_ptrs.type=*(void**)p5;
}
template<typename T>
void dump_ptr(T* p){ dbprintf("%p{%p %p %p %p}\n",(void*)p,0[(void**)p],1[(void**)p],2[(void**)p],3[(void**)p]);}

void verify_expr_op(const Node* p){
	lazy_cache_vtable_ptrs();
	ASSERT(g_vtable_ptrs.expr_op==*(void**)p)
}
void verify_expr_block(const Node* p){
	lazy_cache_vtable_ptrs();
	ASSERT(g_vtable_ptrs.expr_block==*(void**)p)
}
void verify_expr_fn_def(const Node* p){
	lazy_cache_vtable_ptrs();
	ASSERT(g_vtable_ptrs.expr_fn_def==*(void**)p)
}
void verify_expr_ident(const Node* p){
	lazy_cache_vtable_ptrs();
	ASSERT(g_vtable_ptrs.expr_ident==*(void**)p)
}
void verify_type(const Node* p){
	lazy_cache_vtable_ptrs();
	ASSERT(g_vtable_ptrs.type==*(void**)p)
}
void verify_all_sub(){g_pRoot->verify();}
void dbprintf(const char* str, ... )
{
#ifdef DEBUG
	char tmp[1024];
	va_list arglist;
	
	va_start( arglist, str );
	vsprintf(tmp, str, arglist );
	va_end( arglist );
	printf("%s",tmp);
#endif
}
void dbprintf(SrcPos& pos){
	dbprintf("%s:%d:%d:",g_filename,pos.line,pos.col);
}
void dbprintf(Name& n){
	dbprintf("%s",str(n));
}
void dbprintf(Node* n){n->dump(-1);}

ResolvedType resolve_make_fn_call(Expr* receiver,ExprBlock* block/*caller*/,Scope* scope,const Type* desired,int flags);

void print_tok(Name n){
	dbprintf("%s ",getString(n));
};

bool g_lisp_mode=false;
int g_size_of[]={
	0,
	4,4,8,1,2,4,8,1,2,4,8,16,1,
	2,4,8,161,8,0,-1,-1,-1,8,8,8
};
int g_raw_types[]={
	4|RT_SIGNED|RT_INTEGER,
	4|RT_INTEGER,
	8|RT_INTEGER,
	1|RT_SIGNED|RT_INTEGER,
	2|RT_SIGNED|RT_INTEGER,
	4|RT_SIGNED|RT_INTEGER,
	8|RT_SIGNED|RT_INTEGER,
	1|RT_INTEGER,
	2|RT_INTEGER,
	4|RT_INTEGER,
	8|RT_INTEGER,
	16|RT_INTEGER,
	1|RT_INTEGER,
	2|RT_FLOATING,
	4|RT_FLOATING,
	8|RT_FLOATING,
	16|RT_FLOATING|RT_SIMD,
	1|RT_INTEGER,
	8|RT_POINTER,
	0|0,
	0|0,
	0|0,
	0|0,
	0|0,
	0|0,
	0|0,
	0|0,
};
const char* g_token_str[]={
	"",
	"int","uint","size_t","i8","i16","i32","i64","u8","u16","u32","u64","u128","bool",
	"half","float","double","float4",
	"char","str","void","voidptr","one","zero","nullptr","true","false",
	"auto","pTr","ref","Self",
	"tuple","__NUMBER__","__TYPE__","__IDNAME__",
	
	"print___","fn","struct","class","trait","virtual","static","extern", "enum","array","vector","union","variant","with","match","where","sizeof","typeof","nameof","offsetof", "this","self","super","vtableof","closure",
	"let","var",
	"const","mut","volatile",
	"while","if","else","do","for","in","return","break","continue",
	"(",")",
	"{","}",
	"[","]",
	"<[","]>",
	"->",".","?.","=>","<-","::","<->",			//arrows,accessors
	"|>","<|","<.>","<$>","<:",":>",			// some operators we might nick from other langs

	":","as","new","delete",
	"+","-","*","/",					//arithmetic
	"&","|","^","%","<<",">>","?:","?>","?<",					//bitwise
	"<",">","<=",">=","==","!=",		//compares
	"&&","||",		//logical
	"=",":=","=:","@",
	"+=","-=","*=","/=","&=","|=","^=","%=","<<=",">>=", // assign-op
	".=",	// linklist follow ptr.=next
	"++","--","++","--", //inc/dec
	"-","*","&","!","~", // unary ops
	"*?","*!","&?","~[]","[]","&[]", // special pointers?
	",",";",";;",
	"...","..",
	"_","",
	"\"C\"","__vtable_ptr","__env_ptr","__env_i8_ptr",
	NULL,	
};

int g_tok_info[]={
	0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,// int types
	0,0,0,0,  //floats
	0,0,0,0,0,0,0,0,0,
	0,0,0,0,
	0,0,0,0,			// type modifiers
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0, 0,0,0,0,0, // keywords
	0,0,			// let,var
	0,0,0,			// modifiers const,mut,volatile
	0,0,0,0,0,0,0,0,0,  // while,if,else,do,for,in,return,break
	0,0, //( )
	0,0, //{ }
	0,0, // [ ]
	0,0, //<[Type]>
	READ|10,READ|2,READ|2,READ|10,READ|10,READ|13,WRITE|10,	   // dots, arrows
	READ|17,READ|17,READ|17,READ|5,READ|17,READ|17,	// unusual stuff
	READ|9,READ|9, READ|UNARY|ASSOC|3, READ|UNARY|ASSOC|3,
	READ|6,READ|6,READ|5,READ|5,		//arithmetic
	READ|8,READ|7,READ|8,READ|6,READ|9,READ|9,READ|9,READ|9,READ|9,	//bitwise
	READ|8,READ|8,READ|8,READ|8,READ|9,READ|9,	// COMPARES
	READ|13,READ|14,	//logical
	WRITE_LHS|READ_RHS|ASSOC|16,WRITE_LHS|READ_RHS|ASSOC|16,WRITE_LHS|READ_RHS|ASSOC|16,0, // assignment
	
	WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16,WRITE_LHS|READ|ASSOC|16, // assign-op
	WRITE_LHS|READ|ASSOC|16, // dot-assign
	MODIFY|PREFIX|UNARY|2,MODIFY|PREFIX|UNARY|2,MODIFY|UNARY|ASSOC|3,MODIFY|UNARY|ASSOC|3, // post/pre inc/dec
	READ|UNARY|PREFIX|3,READ|UNARY|PREFIX|3,READ|UNARY|PREFIX|3,READ|UNARY|PREFIX|3,READ|UNARY|PREFIX|3, //unary ops
	READ|UNARY|ASSOC|3, READ|UNARY|ASSOC|3, READ|UNARY|ASSOC|3, READ|UNARY|ASSOC|3, READ|UNARY|ASSOC|3,READ|UNARY|ASSOC|3, /// special pointers
	0,0,17, // delim
	0,
	0,0,
	0, //placeholder
	0,0,0,0,0
};
bool is_ident(Name tok){return tok>=IDENT;}
bool is_type(Name tok){return tok<T_NUM_TYPES;}
bool is_operator(Name tok){ return tok>=ARROW && tok<COMMA;}
bool is_condition(Name tok){
	return (tok>=LT && tok<=LOG_OR);
}
bool is_comparison(Name tok){
	return (tok>=LT && tok<=NE);
}
bool is_callable(Name tok) { return (tok==FN || tok==CLOSURE);}
int operator_flags(Name tok){return g_tok_info[index(tok)];}
int precedence(Name ntok){auto tok=index(ntok);return tok<IDENT?(g_tok_info[tok] & PRECEDENCE):0;}
int is_prefix(Name ntok){auto tok=index(ntok);return tok<IDENT?(g_tok_info[tok] & (PREFIX) ):0;}
int arity(Name ntok){auto tok=index(ntok);return  (tok<IDENT)?((g_tok_info[tok] & (UNARY) )?1:2):-1;}
int is_right_assoc(Name ntok){auto tok=index(ntok);return (tok<IDENT)?(g_tok_info[tok]&ASSOC):0;}
int is_left_assoc(Name ntok){auto tok=index(ntok);return (tok<IDENT)?(!(g_tok_info[tok]&ASSOC)):0;}

Name get_prefix_operator(Name tok) {
	auto itok=index(tok);
	if (itok>IDENT) return tok;
	switch (itok){
	case POST_INC: return Name(PRE_INC);
	case POST_DEC: return Name(PRE_DEC);
	case SUB: return Name(NEG);
	case MUL: return Name(DEREF);
	case AND: return Name(ADDR);
	default: return tok;
	}
}
Name get_infix_operator(Name tok) {
	auto itok=index(tok);
	if (itok>IDENT) return tok;
	switch (itok){
	case PRE_INC: return Name(POST_INC);
	case PRE_DEC: return Name(POST_DEC);
	case NEG: return Name(SUB);
	case DEREF: return Name(MUL);
	case ADDR: return Name(AND);
	default: return tok;
	}
}
typedef LLVMOp LLVMOp2[2];
LLVMOp2 g_llvm_ops[]= {
	{{-1,"add","add"},{-1,"fadd","fadd"}},
	{{-1,"sub","sub"},{-1,"fsub","fsub"}},
	{{-1,"mul","mul"},{-1,"fmul","fmul"}},
	{{-1,"div","div"},{-1,"fdiv","fdiv"}},
	{{-1,"and","and"},{-1,"fand","fand"}},
	{{-1,"or","or"},{-1,"fadd",""}},
	{{-1,"xor","xor"},{-1,"fadd",""}},
	{{-1,"srem","rem"},{-1,"fadd",""}},
	{{-1,"shl","shl"},{-1,"fadd",""}},
	{{-1,"ashr","shr"},{-1,"fadd",""}},
};
LLVMOp2 g_llvm_logic_ops[]= {
	{{-1,"and",""},{-1,"fadd",""}},
	{{-1,"or",""},{-1,"fadd",""}},
};
LLVMOp2 g_llvm_cmp_ops[]= {
	{{-1,"icmp slt","icmp ult"},{-1,"fcmp ult","fcmp ult"}},
	{{-1,"icmp sgt","icmp ult"},{-1,"fcmp ugt","fcmp ugt"}},
	{{-1,"icmp sle","icmp ult"},{-1,"fcmp ule","fcmp ule"}},
	{{-1,"icmp sge","icmp ult"},{-1,"fcmp uge","fcmp uge"}},
	{{-1,"icmp eq","icmp eq"},{-1,"fcmp ueq","fcmp ueq"}},
	{{-1,"icmp ne","icmp ne"},{-1,"fcmp une","fcmp une"}},
};
//const char* g_llvm_type[]={
//	"i32","i32","i1","float","i8","i8*"
//};
const char* g_llvm_type_str[]={
	"i32","u32","i64",
	"i8","i16","i32","i64","u8","u16","u32","u64","u128","i1",
	"half","float","double","< 4 x float >", "i8", "i8*","void","void*",
	nullptr
};
const char* get_llvm_type_str(Name n_type_name){
	auto tname=index(n_type_name);
	if (tname>=INT && tname<=(VOIDPTR)){
		return g_llvm_type_str[tname-INT];
	}
	return getString(tname);
}

const LLVMOp* get_op_llvm(Name ntok,Name ntype){
	auto tok=index(ntok); auto type=index(ntype);
	int ti=(type==FLOAT||type==DOUBLE||type==FLOAT)?1:0;
	if (tok>=ADD && tok<=SHR)
		return &g_llvm_ops[tok-ADD][ti];
	if (tok>=ADD_ASSIGN && tok<=SHR_ASSIGN)
		return&g_llvm_ops[tok-ADD_ASSIGN][ti];
	if (tok>=LOG_AND && tok<=LOG_OR)
		return &g_llvm_logic_ops[tok-LOG_AND][ti];
	if (tok>=LT && tok<=NE)
		return &g_llvm_cmp_ops[tok-LT][ti];
	return 0;
}


StringTable::StringTable(const char** initial){
	verbose=false;
	nextId=0;
//	for (int i=0; g_token_str[i];i++) {
//		auto tmp=g_token_str[i];
//		get_index(tmp,tmp+strlen(tmp));
//	}
	nextId=NUM_STRINGS;
	index_to_name.resize(NUM_STRINGS);
	for (int i=0; i<index_to_name.size(); i++) {
		index_to_name[i]=std::string(g_token_str[i]);
		names.insert(std::make_pair(index_to_name[i],i));
	}
	ASSERT(nextId==NUM_STRINGS);
}
int StringTable::get_index(const char* str, const char* end,char flag) {
	if (!end) end=str+strlen(str);
	auto len=(end)?(end-str):strlen(str);
	string s; s.resize(len);memcpy((char*)s.c_str(),str,len);((char*)s.c_str())[len]=0;
	auto ret=names.find(s);
	if (ret!=names.end())	return ret->second;		
	names.insert(std::make_pair(s,nextId));
	index_to_name.resize(nextId+1);
	index_to_name[nextId]=s;
	flags.resize(nextId+1); flags[nextId]=flag;
	if (verbose)
		dbprintf("insert[%d]%s\n",nextId,index_to_name[nextId].c_str());
	return	nextId++;
};

void StringTable::dump(){
	dbprintf("\n");
	for (int i=0; i<this->nextId; i++) {
		dbprintf("[%d]%s\n",i,this->index_to_name[i].c_str());
	}
};

StringTable g_Names(g_token_str);
Name getStringIndex(const char* str,const char* end) {
	return g_Names.get_index(str, end,0);
}
Name strConcat(Name n1, Name n2);
inline Name strConcat(Name n1, Name n2,Name n3){ return strConcat(n1,strConcat(n2,n3));}

Name getStringIndexConcat(Name base, const char* s2){
	char tmp[512];
	snprintf(tmp,511,"%s%s",str(base),s2);
	return getStringIndex(tmp);
}
Name strConcat(Name n1, Name n2){
	// todo - we could optimize the string table around concatenations
	return getStringIndexConcat(n1,str(n2));
}
const char* getString(const Name& n) {
	auto i=index(n);
	return i?g_Names.index_to_name[i].c_str():"";
}
Name getNumberIndex(int num){
	char tmp[32];sprintf(tmp,"%d",num); return g_Names.get_index(tmp,0,StringTable::Number);
}
bool is_number(Name n){
	return g_Names.flags[(int)n]&StringTable::Number;
}

void indent(int depth) {
	for (int i=0; i<depth; i++){dbprintf("\t");};
}
void newline(int depth) {
	if (depth>=0) dbprintf("\n;"); indent(depth);
}
// Even a block is an evaluatable expression.
// it may contain outer level statements.

Type* Node::expect_type() const {
	if (this->m_type)
		return m_type;
	error((const Node*)this,"%s has no type\n", str(name));
	return nullptr;
}
void Node::dump(int depth) const {
	if (!this) return;
	newline(depth);dbprintf("(?)");
}
void Node::dump_top() const {
	if (!this) return;
	dbprintf("%s ", getString(name));
}

int get_typeparam_index(const vector<TParamDef*>& tps, Name name) {
	for (int i=(int)(tps.size()-1); i>=0; i--) {
		if (tps[i]->name==name)
			return i;
	}
	return -1;
}

ResolvedType assert_types_eq(int flags, const Node* n, const Type* a,const Type* b) {
	if (!n->pos.line){
		error(n,"AST node hasn't been setup properly");
	}
	ASSERT(a && b);
	// TODO: variadic args shouldn't get here:
	if (a->name==ELIPSIS||b->name==ELIPSIS)
		return ResolvedType(a,ResolvedType::COMPLETE);
	if (!a->is_equal(b)){
		if (a->is_coercible(b)){
			return ResolvedType(0,ResolvedType::COMPLETE);
		}
		if (!(flags & R_FINAL))
			return ResolvedType(0,ResolvedType::INCOMPLETE);
		
		n->dump(0);
		error_begin(n," type mismatch\n");
		warning(a->get_origin(),"from here:");
		a->dump(-1);
		warning(b->get_origin(),"vs here");
		b->dump(-1);
		error_end(n);
		return ResolvedType(a,ResolvedType::ERROR);
	}
	return ResolvedType(a,ResolvedType::COMPLETE);
}
void verify(const Type* a){
	if (a){
		ASSERT(a->name>=0 && a->name<g_Names.nextId);
		verify(a->sub);
		verify(a->next);
	}
}
void verify(const Type* a,const Type* b){
	verify(a);
	verify(b);
}
void verify(const Type* a,const Type* b,const Type* c){
	verify(a);
	verify(b);
	verify(c);
}
const Type* any_not_zero(const Type* a, const Type* b){return a?a:b;}
ResolvedType propogate_type(int flags,const Node*n, Type*& a,Type*& b) {
	verify(a,b);
	if (!(a || b))
		return ResolvedType(0,ResolvedType::INCOMPLETE);
	if (!a && b) {
		a=b;
		return ResolvedType(a,ResolvedType::COMPLETE);}
	else if (!b && a) {
		b=a;
		return ResolvedType(b,ResolvedType::COMPLETE);
	}
	return assert_types_eq(flags,n, a,b);
}
ResolvedType propogate_type(int flags, Expr *n, Type*& a,Type*& b) {
	verify(a,b);
	propogate_type(flags,(const Node*)n,a,b);
	propogate_type(flags,(const Node*)n,n->type_ref(),b);
	return propogate_type(flags,(const Node*)n,n->type_ref(),a);
}
ResolvedType propogate_type_fwd(int flags,const Node* n, const Type* a,Type*& b) {
	verify(a,b);
	if (!(a || b))
		return ResolvedType(0,ResolvedType::INCOMPLETE);
	if (!a && b){
		return ResolvedType(b,ResolvedType::INCOMPLETE);
	}
	if (!b && a) {
		b=(Type*)a;
		return ResolvedType(a,ResolvedType::COMPLETE);
	}
	return assert_types_eq(flags,n, a,b);
	
	return ResolvedType(b,ResolvedType::INCOMPLETE);
}
ResolvedType propogate_type_fwd(int flags,Expr* e, const Type*& a) {
	return propogate_type_fwd(flags,e, a, e->type_ref());
}
ResolvedType propogate_type(int flags,Expr* e, Type*& a) {
	return propogate_type(flags,e, a, e->type_ref());
}

ResolvedType propogate_type(int flags,const Node* n, Type*& a,Type*& b,Type*& c) {
	verify(a,b,c);
	int ret=ResolvedType::COMPLETE;
	ret|=propogate_type(flags,n,a,b).status;
	ret|=(c)?propogate_type(flags,n,b,c).status:ResolvedType::INCOMPLETE;
	ret|=(c)?propogate_type(flags,n,a,c).status:ResolvedType::INCOMPLETE;
	const Type* any=any_not_zero(a,any_not_zero(b,c));
	return ResolvedType(any,ret);
}
ResolvedType propogate_type_fwd(int flags,const Node* n,const Type*& a,Type*& b,Type*& c) {
	verify(a,b,c);
	int ret=ResolvedType::COMPLETE;
	ret|=propogate_type_fwd(flags,n,a,b).status;
	ret|=propogate_type_fwd(flags,n,a,c).status;
	ret|=propogate_type(flags,n,b,c).status;
	return ResolvedType(any_not_zero(a,any_not_zero(b,c)),ret);
}
ResolvedType propogate_type(int flags,const Node* n, ResolvedType& a,Type*& b) {
	verify(a.type,b);
	a.combine(propogate_type(flags,n, a.type,b));
	return a;
}
ResolvedType propogate_type(int flags,Expr* e, ResolvedType& a) {
	return propogate_type(flags,e, a, e->type_ref());
}

ResolvedType propogate_type(int flags,const Node* n,ResolvedType& a,Type*& b,const Type* c) {
	verify(a.type,b,c);
	a.combine(propogate_type_fwd(flags,n, c,b));
	a.combine(propogate_type(flags,n, a.type,b));
	return a;
}
ExprStructDef* dump_find_struct(Scope* s, Name name){
	for (;s;s=s->parent_or_global()){
		dbprintf("find %s in in scope %s\n",getString(name),s->name());
		for (auto ni=s->named_items;ni;ni=ni->next){
			dbprintf("name %s\n",getString(ni->name));
			for (auto sd=ni->structs; sd;sd=sd->next_of_name) {
				dbprintf("? %s\n",getString(sd->name));
				
			}
		}
	}
	return nullptr;
}

// compile time function in type system, available when you use
// typeparams.
//
// type 'grammar' is the same, its just the operators do different things
//
// type Option[x]->Some[x]|None;	// union.
//
// type WinFrame=Window&Frame;		// get all the methods
//
// type Bar=Foo[a,b];  just like typedef.
// type Mul[a,b]->Vec3[Mul[a,b]]  // like applying a function
// type Mul[float,float]->float
// type Foo[a]->tuple[a.node,a.foo] // accessors for associated types?

size_t ArgDef::size()const {
	return this->type()?this->type()->size():0;
}
size_t ArgDef::alignment() const	{
	return type()->alignment();
}//todo, 	size_t		alignment() const			{ return type()->alignment();}//todo, eval templates/other structs, consider pointers, ..
ResolvedType ArgDef::resolve(Scope* sc, const Type* desired, int flags){
	dbg_resolve("resolving arg %s\n",this->name_str());
	propogate_type_fwd(flags,this,desired,this->type_ref());
	if (this->type()){
		this->type()->resolve(sc,desired,flags);
	}
	if (this->default_expr){this->default_expr->resolve(sc,this->type(),flags);}
	return ResolvedType(this->type(), ResolvedType::COMPLETE);
}
const char* Node::get_name_str() const{
	if (!this) return "(no_type)";
	return getString(this->name);
}

	//todo: generic heirarchy equality test, duplicate code detection?
bool type_compare(const Type* t,int a0, int a1){
	if (t)
		if (t->name==a0)
			if (t->sub)
				if (t->sub->name==a1)
					return true;
	return false;
}
void Node::set_type(const Type* t)
{	::verify(t);
	if (this->m_type){
		if (this->m_type->is_equal(t))
			return ;
#if DEBUG>=2
		dbprintf("changing type?\n");
		this->m_type->dump(-1);newline(0);
		dbprintf("to..\n");
		t->dump(-1);
		newline(0);
#endif
		//ASSERT(this->m_type==0);
	}
	this->m_type=(Type*)t;
};

void dump_typeparams(const vector<TParamDef*>& ts) {
	bool a=false;
	if (ts.size()==0) return;
	dbprintf("[");
	for (auto t:ts){
		if (a)dbprintf(",");
		print_tok(t->name);if (t->defaultv){dbprintf("=");t->defaultv->dump(-1);}
		a=true;
	}
	dbprintf("]");
}

//void find_printf(const char*,...){};
#define find_printf dbprintf
int num_known_arg_types(vector<Expr*>& args) {
	int n=0; for (auto i=0; i<args.size(); i++) {if (args[i]->get_type()) n++;} return n;
}

//void match_generic_type_param_sub(const vector<TParamDef>& tps, vector<Type*>& mtps, const Type* to_match, const Type* given) {
	
//}

int match_typeparams_from_arg(vector<TParamVal*>& matched_tps, const vector<TParamDef*>& fn_tps,  const Type* fn_arg, const Type* given_arg)
{
	int ret_score=0;
//	if (!fn_arg && !given_arg) return 0;
	// if either is unspecified.. match anything there
	// TODO:
	if (!fn_arg) return 0;
	if (!given_arg) return 0;
	
	dbg_fnmatch("%s/s ",str(fn_arg->name),str(given_arg->name));
	if (fn_arg->sub || given_arg->sub) {
		dbg_fnmatch("[");
		for (const Type* sub1=fn_arg->sub,*sub2=given_arg->sub; sub1&&sub2; sub1=sub1->	next,sub2=sub2->next) {
			ret_score+=match_typeparams_from_arg(matched_tps,fn_tps, sub1, sub2);
		}
		dbg_fnmatch("]");
	}
	// if either is 'AUTO' - consider it ok.
	if (fn_arg->name==AUTO) return ret_score;
	if (given_arg->name==AUTO) return ret_score;

	int ti = get_typeparam_index(fn_tps, fn_arg->name);
	if (ti>=0) {
		// Is this a generic typeparam?
		if (matched_tps[ti]==0){ // new typeparam?
			matched_tps[ti]=(Type*)given_arg;
			return ret_score+1;
		}
		else if (!(matched_tps[ti]->is_equal(given_arg))) {// or we already found it - match..
//			dbg_fnmatch("match %s !=%s\n",str(fn_arg->name),str(given_arg->name));
#if DEBUG>=3
			matched_tps[ti]->dump(-1);
			given_arg->dump(-1);
#endif
			return ret_score-1000;
		}
	} else {
		// concrete types - compare absolutely
		if (fn_arg->name != given_arg->name) {
			dbg_fnmatch("\nmatch %s !=%s\n",str(fn_arg->name),str(given_arg->name));
			return ret_score-1000;	// mismatch is instant fail for this candidate.
		}
	}
	return ret_score;
}

int match_typeparams(vector<TParamVal*>& matched, const ExprFnDef* f, const ExprBlock* callsite){
	// TODO: allow the types to feedback in the math
	matched.resize(f->typeparams.size());
	int score=0;
#if DEBUG>=2
	callsite->dump(0);newline(0);
#endif
	for (int i=0; i<f->typeparams.size();i++) matched[i]=0;
	for (int i=0; i<callsite->argls.size(); i++) {
#if DEBUG>=2
		f->args[i]->type()->dump_if(-1); newline(0);
		callsite->argls[i]->type()->dump_if(-1); newline(0);
#endif
		score+=match_typeparams_from_arg(matched,f->typeparams, f->args[i]->type(), callsite->argls[i]->type());
	}
	score+=match_typeparams_from_arg(matched, f->typeparams, f->ret_type, callsite->type());
	dbg_fnmatch("score matching gets %d\n",score);
	return score;
}
void FindFunction::dump()
{
	for (int i=0; i<candidates.size();i++){
		dbprintf("candidate %d for %s: %d %p score=%d\n",i, str(name),candidates[i].f->pos.line, candidates[i].f->instance_of, candidates[i].score);
	}
}

void FindFunction::insert_candidate(ExprFnDef* f,int score){
	verify_all();
	if (candidates.size()>=max_candidates){
		for (int i=0; i<candidates.size()-1;i++){
			candidates[i]=candidates[i+1];
		}
		candidates.resize(candidates.size()-1);
	}
	verify_all();
	for(int i=0; i<candidates.size();i++){
		if (candidates[i].f==f)return;// no duplicate?!
		if (candidates[i].score>score) {
			candidates.resize(candidates.size()+1);
			verify_all();
			for (size_t j=candidates.size()-1;j>i; j--){
				candidates[j]=candidates[j-1];
			}
			verify_all();
			candidates[i]=Candidate{f,score};
			verify_all();
			return;
		}
	}
	candidates.push_back(Candidate{f,score});
	verify_all();
}

void FindFunction::consider_candidate(ExprFnDef* f) {
	dbg_fnmatch("consider candidate %d %s\n",f->pos.line,str(f->name));
	verify_all();
	for (auto& c:this->candidates){
		if (c.f==f)
			return;
	}// ? shouldn't happen.
	if (f->type_parameter_index(f->name)<0)
		if (f->name!=name && name!=PLACEHOLDER)
			return ;
	if (!f->is_enough_args((int)args.size()) || f->too_many_args((int)args.size())){
		if (0==(this->flags&R_FINAL)) return;
	}
	// TODO: may need to continually update the function match, eg during resolving, as more types are found, more specific peices may appear?
	// Find max number of matching arguments
	
	verify_all();
	vector<Type*> matched_type_params;
	for (int i=0; i<f->typeparams.size(); i++){matched_type_params.push_back(nullptr);}

	int score=0;
	// +1 for any matching arg type regardless of placement,bonus if aprox right order
	for (int i=0; i<args.size(); i++) {
		auto at=args[i]->get_type(); if (!at) continue;
		for (int jj=i; jj<f->args.size(); jj++) {
			auto j=jj%args.size();
			if (f->args[j]->get_type()->is_equal(at)){
				if (j==i) score+=(1+args.size()-i); // args in right pos score higher
				score++;
				break;
			}
		}
	}
	if (name==PLACEHOLDER){
//		score-=1000;
	}
	else if (!f->is_enough_args((int)args.size()) || f->too_many_args((int)args.size())){
		score-=1000;
		insert_candidate(f,score);
		return;
	}

	if (f->variadic && args.size()> f->args.size())
		score=1;	// variadic functoin can match anything?
	if (!f->typeparams.size())
	for (int i=0; i<args.size() && i<f->args.size(); i++) {
		if (!f->args[i]->get_type() || (!args[i])) {
			score++; //1 point for an 'any' arg on either side
		} else{
			// both args are given:
			if (f->args[i]->get_type()->is_equal(args[i]->get_type())) {
				score+=10;// 1 exact match worth more than any number of anys
			} else{
				//if (!is_generic_type(f->typeparams,f->args[i]->get_type())
				
				{
				// instant fail for incorrect concrete arg
				//TODO consider conversion operators here.
					score-=1000;
				//	if (candidates.size()>=4)
				//		return;
				}
			}
		}
	}
	// find generic typeparams..
	if (f->typeparams.size()){
#if DEBUG>=2
		dbg_fnmatch("%s score=%d; before typaram match\n",str(f->name),score);
		dbg_fnmatch("callsite:\n");
		for (int i=0; i<callsite->as_block()->argls.size();i++) {
			dbg_fnmatch("arg %s:",  str(args[i]->name));
			callsite->as_block()->argls[i]->type()->dump_if(-1);
			dbg_fnmatch("\tvs\t");
			f->args[i]->type()->dump_if(-1);
			dbg_fnmatch("\n");
		}
		dbg_fnmatch("\n");
#endif
		for (int i=0; i<args.size() && i<f->args.size(); i++) {
			score+=match_typeparams_from_arg(matched_type_params,f->typeparams, f->args[i]->get_type(), args[i]->get_type());
		}
		score+=match_typeparams_from_arg(matched_type_params, f->typeparams,f->ret_type,ret_type);
		dbg_fnmatch("typaram matcher for %s\n",f->name_str());
		dbg_fnmatch("%s:%d: %s\n",g_filename,f->pos.line,str(f->name));
		dbg_fnmatch("%s score=%d; matched typeparams{:-\n",str(f->name),score);
		for (auto i=0; i<f->typeparams.size(); i++){
			dbg_fnmatch("[%d]%s = %s;\n", i,str(f->typeparams[i]->name),matched_type_params[i]?str(matched_type_params[i]->name):"" );
			}
		dbg_fnmatch("}\n");
		dbg_fnmatch("\n");
		
	}
	verify_all();
	// fill any unmatched with defaults?

	// consider return type in call.
	if (ret_type)
		if (f->get_type()->is_equal(ret_type)) score+=100;

	if (f->name==name) score*=100; // 'named' functions always win over un-named forms eg F[F,X](a:X),we may use unnamed to implement OOP..
	// insert candidate
	verify_all();
	insert_candidate(f,score);
	verify_all();
}
void FindFunction::find_fn_sub(Expr* src) {
	verify_all();
	if (auto sb=dynamic_cast<ExprBlock*>(src)) {
		for (auto x:sb->argls) {
			find_fn_sub(x);
		}
	} else if (auto f=dynamic_cast<ExprFnDef*>(src)){
		if (verbose)
			dbg_fnmatch("consider %s %d for %s %d\n",f->name_str(),f->pos.line, this->callsite->name_str(),this->callsite->pos.line);
		consider_candidate(f);
		int i=0;
		for (auto ins=f->instances; ins; ins=ins->next_instance,i++) {
			dbg_fnmatch("%s ins=%d\n",f->name_str(),i);
			consider_candidate(ins);
		}
	}
	verify_all();
}
void FindFunction::find_fn_from_scopes(Scope* s,Scope* ex)
{
	if (name!=PLACEHOLDER){
		if (auto fname=s->find_named_items_local(name)){
			for (auto f=fname->fn_defs; f;f=f->next_of_name) {
				find_fn_sub((Expr*)f);
			}
		}
	} else{
		for (auto ni=s->named_items; ni; ni=ni->next){
			for (auto f=ni->fn_defs; f;f=f->next_of_name) {
				find_fn_sub((Expr*)f);
			}
		}
	}
	for (auto f=s->templated_name_fns; f;f=f->next_of_name) {
		find_fn_sub((Expr*)f);
	}
	for (auto sub=s->child; sub; sub=sub->next) {
		if (sub==ex) continue;
		find_fn_from_scopes(sub,ex);
	}
}

void dbprint_find(const vector<ArgDef*>& args){
	dbprintf("\n;find call with args(");
	for (int i=0; i<args.size(); i++) {dbprintf(" %d:",i);dbprintf("%p\n",args[i]);if (args[i]->get_type()) args[i]->get_type()->dump(-1);}
	dbprintf(")\n");
}
template<typename T>
void dump(vector<T*>& src) {
	for (int i=0; i<src.size(); i++) {
		dbprintf(src[i]->dump());
	}
}
ResolvedType StructInitializer::resolve(const Type* desiredType,int flags) {

	auto sd=sc->find_struct(si->call_expr);
	if (!sd){
		if (flags&R_FINAL){
			error(si->call_expr,"can't find struct %s",si->call_expr->name_str());
		}
		return ResolvedType();
	}
	auto local_struct_def=dynamic_cast<ExprStructDef*>(si->call_expr);
	if (local_struct_def)sc->add_struct(local_struct_def); // todo - why did we need this?
	if (!si->type()){
		si->set_type(new Type(sd));
	}
	si->call_expr->def=sd;
	si->def=sd;
	// assignment forms are expected eg MyStruct{x=...,y=...,z=...} .. or can we have MyStruct{expr0,expr1..} equally?
	//int next_field_index=0;
	// todo:infer generic typeparams - adapt code for functioncall. we have struct fields & struct type-params & given expressions.
	int named_field_index=-1;
	// todo encapsulate StructInitializer to reuse logic for codegen
	field_indices.reserve(si->argls.size());
	int field_index=0;
	for (auto i=0; i<si->argls.size(); i++)  {
		auto a=si->argls[i];
		auto op=dynamic_cast<ExprOp*>(a);
		ArgDef* field=nullptr;
		Type*t = nullptr;
		if (op&&(op->name==ASSIGN||op->name==COLON||op->name==LET_ASSIGN)){
			field=sd->find_field(op->lhs);
			op->rhs->resolve(sc,field->type(),flags); // todo, need type params fwd here!
			propogate_type(flags,op,op->lhs->type_ref(),op->rhs->type_ref());
			//				propogate_type(flags,op,op->rhs->type_ref());
			op->lhs->def=field;
			named_field_index=sd->field_index(op->lhs);
			this->value.push_back(op->rhs);
			t=op->rhs->type();
		}else if (named_field_index==-1){
			if (field_index>=sd->fields.size()){
				error(a,sd,"too many fields");
			}
			field=sd->fields[field_index++];
			this->value.push_back(a);
			a->resolve(sc,field->type(),flags); // todo, need generics!
			t=a->type();
		}else{
			error(a,"named field expected");
		}
		this->field_refs.push_back(field);
		this->field_indices.push_back(field_index);
		if (local_struct_def){
			// special case :( if its' an inline def, we write the type. doing propper inference on generic structs have solved this stupidity.
			if (!local_struct_def->fields[i]->type()){
				local_struct_def->fields[i]->type()=t;
			}
		}
	}
//	?. // if (this) return this->.... else return None.
	
	return propogate_type_fwd(flags,si, desiredType);
}


ResolvedType resolve_make_fn_call(Expr* receiver,ExprBlock* block/*caller*/,Scope* scope,const Type* desired,int flags) {
	verify_all();

	int num_resolved_args=0;
	for (int i=0; i<block->argls.size(); i++) {
		block->argls[i]->resolve(scope,nullptr,flags);
		if (block->argls[i]->type()) num_resolved_args++;
	}
#if DEBUG>4
	for (int i=0; i<block->argls.size(); i++) {
		dbprintf("arg[i]=",i);
		block->argls[i]->dump_if(-1);newline(0);
	}
#endif
	if (receiver){
		receiver->resolve(scope,nullptr,flags); if (receiver->type()) num_resolved_args++;
	}
	
	if (block->get_fn_call() && num_resolved_args==block->argls.size())
		return ResolvedType(block->get_fn_call()->ret_type,ResolvedType::COMPLETE);

//	11ASSERT(block->call_target==0);
	// is it just an array access.
	if (block->is_subscript()){
		auto object_t=block->call_expr->resolve(scope,nullptr,flags);
		auto index_t=block->argls[0]->resolve(scope,nullptr,flags);
		//find the method "index(t,index_t)"
		// for the minute, just assume its' an array
		// TODO: operator overload.

		if (object_t.type && index_t.type) {
			ASSERT(object_t.type->is_array() || object_t.type->is_pointer());
			return ResolvedType(object_t.type->sub,ResolvedType::COMPLETE);
		}
		return ResolvedType();
	}
	verify_all();

	vector<Expr*> args_with_receiver;
	if (receiver) args_with_receiver.push_back(receiver);
	args_with_receiver.insert(args_with_receiver.end(),block->argls.begin(),block->argls.end());

	ExprFnDef* call_target = scope->find_fn(block->call_expr->as_name(),block,args_with_receiver, desired,flags);
	auto fnc=call_target;
	if (!call_target){
		return ResolvedType();
	}
	verify_all();
	if (call_target!=block->get_fn_call()) {
		block->call_expr->set_def(call_target);
		if (block->get_fn_call()) {
			error(block,"call target changed during resolving, we're not sure how to handle this yet\n");
			block->scope=0; // todo delete.
		} else {
			block->scope=0; // todo delete.
		}
		block->set_def(call_target);
		ASSERT(block->def==block->call_expr->def);
		if (call_target->resolved) {
			Type * fnr=call_target->return_type();
			return propogate_type_fwd(flags,block, desired, block->type_ref(),fnr);
		}
			// add this to the targets' list of calls.
		int num_known_types=(desired?1:0)+num_known_arg_types(block->argls);
		bool isg=fnc->is_generic();
		if (!(isg && num_known_types)) {
			// concrete function: we can just take return type.
			auto rt=fnc->return_type();
			return propogate_type_fwd(flags,block, desired, rt,block->type_ref());
		}
		{
			int once=false; if(!once++){
			dbprintf("TODO decide if we should allow genrics to instantiate earlry\n");
			dbprintf("should we propogate types or");
			dbprintf("wait till the environment propogates them");
			dbprintf("to select");
			dbprintf("restrict to forward inference for this case?");
				
			}
		}
		return ResolvedType();
		// generic function, and we have some types to throw in...
		// if its' a generic function, we have to instantiate it here.
/*
		if (!block->scope) {
//			ASSERT(0 && "this is bs");
//			auto sc=new Scope;
//			block->scope=sc;
//			scope->push_child(sc);
//			sc->owner=call_target;//TODO: this is dodgy.
			// do we need to distinguish an inline instance from a global instance.
			block->next_of_call_target = call_target->callers;
			call_target->callers =block;
		}
		// create a local for each supplied argument, using its type..
		// note that vars should be able to propogate inside too???
		// TODO-inlining generic call? or what?
		Scope* fsc=block->scope;
		for (int i=0; i<block->argls.size() && i<fnc->args.size(); i++) {
			auto input_type=block->argls[i]->get_type();
			auto v=fsc->create_variable(fnc->args[i], fnc->args[i]->name,VkArg);
			auto argtype=fnc->args[i]->get_type();
			if (!v->type()){
				v->type(argtype?argtype:input_type);
			} else {
				// read the type out from the function invocation, really.
				if (block->argls[i]->get_type()) {ASSERT(input_type->eq(v->type()));}
				else block->argls[i]->type(v->type());
			}
			// and stuff a default expression in for any not called..
		}
 
		auto ret=call_target->resolve_call(fsc,desired,flags);
		verify_all();
		return propogate_type(flags,block, ret);
 */
	}
	else  {
		if (flags &1) error(block,"can't resolve call\n");
		verify_all();
		return ResolvedType();
	}
	verify_all();
}

Type* CaptureVars::get_elem_type(int i){
	auto v=vars;
	for (; v&&i>0; i--,v=v->next_of_capture);
	return v->type();
}

Name CaptureVars::get_elem_name(int i){
	auto v=vars;
	for (; v&&i>0; i--,v=v->next_of_capture);
	return v->name;
}
int CaptureVars::get_elem_count(){
	auto v=vars;
	int i=0;
	for (; v; i++,v=v->next_of_capture);
	return i;
}

void CaptureVars::coalesce_with(CaptureVars *other){
	// remap all functions that use the other to point to me
	while (other->capture_by){
		auto f=other->capture_by; // pop others' f
		other->capture_by=f->next_of_capture;

		f->next_of_capture= this->capture_by;	// push f to this' capture_by list.
		this->capture_by=f;
		f->my_capture=this;
	}
	// steal other's variables
	while (other->vars){
		auto v=other->vars;		// pop other's var
		other->vars=v->next_of_capture;

		v->next_of_capture=this->vars; // push to this
		this->vars=v;
		v->capture_in=this;
	}
}

void call_graph(Node* root,Scope* scope) {
}

void unexpected(int t){error(0,"unexpected %s\n",getString(t));}

void
gather_vtable(ExprStructDef* d) {
}
int TypeParamXlat::typeparam_index(const Name& n) const{
	for (int i=0; i<this->typeparams.size(); i++){
		if (this->typeparams[i]->name==n) return i;
	}
	return -1;
}
void TypeParamXlat::dump(int depth)const{
	dbprintf("[");
	for (auto i=0; i<this->typeparams.size();i++){
		if (i)dbprintf(",");
		dbprintf("%s=",str(this->typeparams[i]->name));
		this->given_types[i]->dump_if(-1);
	}
	dbprintf("]");
}
bool type_params_eq(const vector<Type*>& a, const vector<Type*>& b) {
	if (a.size()!=b.size()) return false;
	for (int i=0; i<a.size(); i++) { if (!a[i]->is_equal(b[i])) return false;}
	return true;
}
bool type_params_eq(const vector<Type*>& a, const Type* tp){
	for (auto
		 i=0; i<a.size() && tp;i++,tp=tp->next){
		if (!a[i]->is_equal(tp))
			return false;
	}
	// TODO- defaults.
	return true;
}
bool	ExprIdent::is_function_name()const	{
	return dynamic_cast<ExprFnDef*>(this->def)!=0;
}
bool		ExprIdent::is_variable_name()const	{
	return dynamic_cast<Variable*>(this->def)!=0;
}



