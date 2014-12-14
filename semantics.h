#pragma once
#include "everywhere.h"
// type inference
ResolvedType propogate_type(int flags,const Node*n, Type*& a,Type*& b);
ResolvedType propogate_type(int flags, Expr *n, Type*& a,Type*& b);
ResolvedType propogate_type_fwd(int flags,const Node* n, const Type* a,Type*& b);
ResolvedType propogate_type_fwd(int flags,Expr* e, const Type*& a);
ResolvedType propogate_type(int flags,Expr* e, Type*& a);
ResolvedType propogate_type(int flags,const Node* n, Type*& a,Type*& b,Type*& c);
ResolvedType propogate_type_fwd(int flags,const Node* n,const Type*& a,Type*& b,Type*& c);
ResolvedType propogate_type(int flags,const Node* n, ResolvedType& a,Type*& b);
ResolvedType propogate_type(int flags,const Node* n,ResolvedType& a,Type*& b,const Type* c);

struct CaptureVars;
// load data->vtb // if this matters it would be inlined
// load vtb->fn
// when the time comes - vtb->destroy()
//                       vtb->trace
struct LLVMType {
	Name name;
	bool is_pointer;
};

struct Node {
private:Type* m_type=0;

public:
	Node*	m_parent=0;					// for search & error messages,convenience TODO option to strip.
	Name name;
	RegisterName reg_name=0;			// temporary for llvm SSA calc. TODO: these are really in Expr, not NOde.
	bool reg_is_addr=false;
	SrcPos pos;						// where is it
	Node(){}
	ExprDef*	def=0;		// definition of the entity here. (function call, struct,type,field);
	Node*		next_of_def=0;
	void set_def(ExprDef* d);
	void clear_def();
	virtual void dump(int depth=0) const;
	virtual ResolvedType resolve(Scope* scope, const Type* desired,int flags){dbprintf("empty? %s resolve not implemented", this->kind_str());return ResolvedType(nullptr, ResolvedType::INCOMPLETE);};
	ResolvedType resolve_if(Scope* scope, const Type* desired,int flags){
		if (this) return this->resolve(scope,desired,flags);
		else return ResolvedType();
	}
	virtual const char* kind_str()const	{return"node";}
	virtual int get_name() const		{return 0;}
	virtual Name get_mangled_name()const {return name;}
	const char* get_name_str()const;
	const char* name_str()const			{return str(this->name);}
//	Name ident() const					{if (this)return this->name;else return 0;}
	virtual Node* clone() const=0;
	Node* clone_if()const				{ if(this) return this->clone();else return nullptr;}
	void dump_if(int d)const			{if (this) this->dump(d);}
	virtual void clear_reg()			{reg_name=0;};
	RegisterName get_reg(CodeGen& cg, bool force_new);
	RegisterName get_reg_new(CodeGen& cg);
	RegisterName get_reg_named(Name baseName, int* new_index, bool force_new);
	RegisterName get_reg_named_new(Name baseName, int* new_index);
	RegisterName get_reg_existing();
	virtual	vector<TParamDef*>*			get_typeparams(){ return nullptr;}
	Node*	parent()					{return this->m_parent;}
	void	set_parent(Node* p)			{this->m_parent=p;}
	virtual CgValue codegen(CodeGen& cg,bool contents);
	virtual bool is_undefined()const										{if (this && name==PLACEHOLDER) return true; return false;}
	virtual void find_vars_written(Scope* s,set<Variable*>& vars ) const	{return ;}
	void find_vars_written_if(Scope*s, set<Variable*>& vars) const{ if(this)this->find_vars_written(s, vars);
	}
	void translate_typeparams_if(const TypeParamXlat& tpx){if (this) this->translate_typeparams(tpx);}
	virtual void translate_typeparams(const TypeParamXlat& tpx){ error(this,"not handled for %s",this->kind_str()); };
	virtual ExprOp* as_op()const			{error(this,"expected op, found %s:%s",str(this->name),this->kind_str());return nullptr;}
	virtual Name as_name()const {
		error(this,"expected named item at node %s kind=%s",str(this->name),this->kind_str());
		return PLACEHOLDER;
	};
	bool is_ident()const;
	virtual ExprStructDef* as_struct_def()const;
	template<typename T> T* as()const{ auto ret= const_cast<T*>(dynamic_cast<T*>(this)); if (!ret){error(this,"expected,but got %s",this->kind_str());} return ret;};
	template<typename T> T* isa()const{ return const_cast<T*>(dynamic_cast<T*>(this));};
	virtual int recurse(std::function<int(Node* f)> f){dbprintf("recurse not implemented\n");return 0;};

	virtual CgValue compile(CodeGen& cg, Scope* sc);
	CgValue compile_if(CodeGen& cg, Scope* sc);
	virtual Node* instanced_by()const{return nullptr;}
	virtual ExprIdent*	as_ident() {return nullptr;}
	virtual ExprFor* 	as_for() {return nullptr;}
	virtual ExprFnDef*	as_fn_def() {return nullptr;}
	virtual const ExprFnDef*	as_fn_def() const {return nullptr;}
	virtual ExprBlock*	as_block() {return nullptr;}
	virtual TParamDef*	as_tparam_def() {return nullptr;}
	ExprBlock* as_block() const{return const_cast<Node*>(this)->as_block();}
	virtual ArgDef* as_arg_def() {return nullptr;}
	virtual Variable* as_variable() {return nullptr;}
	virtual const Variable* as_variable() const {return nullptr;}
	ArgDef*			as_field() {return this->as_arg_def();}
	virtual void verify() {};
	// abstract interface to 'struct-like' entities;
	virtual Type* get_elem_type(int index){error(this,"tried to get elem on name=%s kind=%s",str(this->name),this->kind_str());return nullptr;}
	virtual Name get_elem_name(int index){error(this,"tried to get elem on %s %s",str(this->name),this->kind_str());return nullptr;}
	virtual int get_elem_index(Name name){error(this,"tried to get elem on %s %s",str(this->name),this->kind_str());return -1;}
	virtual int get_elem_count()const{return 0;}
	virtual size_t alignment()const {return 16;} // unless you know more..
	virtual ~Node(){
		error("dont call delete, we haven't sorted out ownership of Types or nodes. compiler implementation doesn't need to free anything. Types will be owned by a manager, not the ast ");
	}
	LLVMType get_type_llvm() const;
	virtual Type* eval_as_type()const		{return nullptr;};
	virtual ExprBlock* is_subscript()const	{return (ExprBlock*)nullptr;}
	virtual bool is_function_name()const	{return false;}
	virtual bool is_variable_name()const	{return false;}
	virtual Scope* get_scope()				{return nullptr;}
	Type* expect_type() const;
	Type* get_type() const		{ if(this) {::verify(this->m_type);return this->m_type;}else return nullptr;}
	Type*& type()				{::verify(this->m_type);return this->m_type;}
	const Type* type()const		{::verify(this->m_type);return this->m_type;}
	void type(const Type* t)	{::verify(t);this->m_type=(Type*)t;}
	void set_type(const Type* t);
	void clear_type(){m_type=0;};
	void force_type_todo_verify(const Type* t){ m_type=const_cast<Type*>(t);}
	Type*& type_ref()			{return this->m_type;}
	void dump_top()const;
};

typedef Type TParamVal;
// Type Parameter, actually Template Parameter as we generalize it.
struct Expr : public Node{					// anything yielding a value
public:
	int visited;					// anti-recursion flag.
};

/// TODO-Anything matchable by the template engine eg Type, Constants, Ident.. (how far do we go unifying templates & macros..)

struct ExprScopeBlock : Expr{
	Scope*		scope=0;
};
struct ExprOp: public Expr{
	Expr	*lhs=0,*rhs=0;
	Node* clone() const;
	void clear_reg()						{lhs->clear_reg(); rhs->clear_reg();}
	ExprOp(Name opname,SrcPos sp, Expr* l, Expr* r){
		pos=sp;
		lhs=l; rhs=r;
		name=opname;
	}
	ExprOp(Name opname,SrcPos sp)			{name=opname; lhs=0; rhs=0;pos=sp;}
	void	dump(int depth) const;
	int		get_operator() const			{return index(this->name);}
	int		get_op_name() const				{return index(this->name);}
	bool	is_undefined()const				{return (lhs?lhs->is_undefined():false)||(rhs?rhs->is_undefined():false);}
	ExprOp*		as_op()const override		{return const_cast<ExprOp*>(this);}
	const char* kind_str()const override		{return"operator";}
	void 		translate_typeparams(const TypeParamXlat& tpx) override;
	ResolvedType resolve(Scope* scope, const Type* desired,int flags) override;
	void 		find_vars_written(Scope* s, set<Variable*>& vars) const override;
	void 		verify() override;
	CgValue compile(CodeGen& cg, Scope* sc);
};

/// 'ExpressionBlock' - expr( expr,expr,...)
///- any group of expressions
///  eg functioncall +args, compound statement, struct-initializer, subscript expr (like fncall)
struct ExprBlock :public ExprScopeBlock{
	// used for function calls and compound statement
	// started out with lisp-like (op operands..) where a compound statement is just (do ....)
	// TODO we may split into ExprOperator, ExprFnCall, ExprBlock
	// the similarity between all is
	
	short	bracket_type;	//OPEN_PARENS,OPEN_BRACES,OPEN_BRACKETS,(ANGLE_BRACKETS?)
	short	delimiter=0;//COMMA, SEMICOLON,SPACES?

	Expr*	call_expr=0;  //call_expr(argls...)  or {argsls...} call_expr[argls..] call_expr{argls}
	vector<Expr*>	argls;
	//ExprFnDef*	call_target=0;
	ExprBlock*	next_of_call_target=0;	// to walk callers to a function
	// these are supposed to be mutually exclusive substates, this would be an enum ideally.
	ExprBlock(){};
	ExprBlock(const SrcPos& p);
	bool	is_compound_expression()const	{return !call_expr && !index(name);}
	bool	is_tuple()const					{return this->bracket_type==OPEN_PAREN && this->delimiter==COMMA;}
	bool	is_struct_initializer()const	{return this->bracket_type==OPEN_BRACE && (this->delimiter==COMMA||this->delimiter==0);}
	bool	is_match() const				{return false;}
	bool	is_function_call()const			{return (this->call_expr!=0) && this->bracket_type==OPEN_PAREN && (this->delimiter==COMMA||this->delimiter==0);}
	bool	is_anon_struct()const			{return this->is_struct_initializer() && !this->call_expr;}
	bool	is_array_initializer()const		{return !this->call_expr && this->bracket_type==OPEN_BRACKET && this->delimiter==COMMA;}
	void	set_delim(int delim)			{delimiter=delim;}
	ExprBlock* is_subscript()const override	{if (this->bracket_type==OPEN_BRACKET && call_expr) return (ExprBlock*) this; return (ExprBlock*)nullptr;}
	ExprFnDef*	get_fn_call()const;
	Name		get_fn_name() const;
	void		dump(int depth) const;
	Node*		clone() const;
	bool		is_undefined()const;
	void		create_anon_struct_initializer();
	void			clear_reg()				{for (auto p:argls)p->clear_reg();if (call_expr)call_expr->clear_reg(); reg_name=0;};
	const char* kind_str() const  override		{return"block";}
	ExprBlock* 		as_block()  override 	{return this;}
	Scope*	get_scope()	override			{return this->scope;}
	void 			verify();
	CgValue 		compile(CodeGen& cg, Scope* sc);
	CgValue 		compile_sub(CodeGen& cg, Scope* sc,RegisterName dst);
	void	translate_typeparams(const TypeParamXlat& tpx) override;
	void	find_vars_written(Scope* s,set<Variable*>& vars )const override;
	ResolvedType	resolve(Scope* scope, const Type* desired,int flags);
	ResolvedType	resolve_sub(Scope* scope, const Type* desired,int flags,Expr* receiver);
};
/// TODO a pattern might become different to Expr
/// simplest must behave like 'ident'
struct Pattern : Node {
	ResolvedType	resolve(Scope* sc, Type* desired, int flags){ASSERT(0 && "dont resolve pattern"); return ResolvedType();}
	Pattern* next=0;
	Pattern* sub=0;
	Node*	clone()const;
	
};

/// rust match, doesn't work yet - unlikely to support everything it does
/// C++ featureset extended with 2way inference can emulate match like boost variant but improved.
struct ExprMatch : Expr {
	const char*		kind_str() const {return "match";}
	CgValue			compile(CodeGen& cg, Scope* sc);
	ResolvedType	resolve(Scope* sc, Type* desired, int flags);
	Expr*		expr=0;
	MatchArm*	arms=0;
	Node*	clone()const;
};
struct MatchArm : ExprScopeBlock {
	/// if match expr satisfies the pattern,
	///  binds variables from the pattern & executes 'expr'
	Pattern*	pattern=0;
	Expr*		cond=0;
	Expr*		body=0;
	MatchArm*	next=0;
	void		dump(int depth);
	Node*		clone() const;
	void		translate_typeparams(const TypeParamXlat& tpx){}
	CgValue		compile_check(CodeGen& cg, Scope* sc, Expr* match_expr,CgValue match_val);
	// todo - as patterns exist elsewhere, so 'compile-bind might generalize'.
	CgValue		compile_bind(CodeGen& cg, Scope* sc, Expr* match_expr,CgValue match_val);
	ResolvedType	resolve(Scope* sc, Type* desired, int flags);
};

enum TypeId{
	// TODO: just equate these to the master token enum. avoid future confusion
	T_NONE,
	T_BOOL,T_INT,T_UINT,T_FLOAT,T_CONST_STRING,T_VOID,T_AUTO,T_ONE,T_ZERO,T_VOIDPTR,T_NULLPTR,
	T_WRONG_PRINT,T_FN,T_STRUCT,T_TUPLE,T_VARIANT,T_KEYWORD,T_NUM_TYPES,
};
bool is_type(int tok);
extern bool g_lisp_mode;
// module base: struct(holds fns,structs), function(local fns), raw module.

struct ExprFlow:Expr{	// control flow statements
};

/// any node that is a Definition, maintains list of refs
struct ExprDef :Expr{
	Node*	refs=0;
	void	remove_ref(Node* ref);
	virtual ExprStructDef* member_of()const{return nullptr;}
};

struct TParamDef: ExprDef{
	TParamVal* bound=0;	// eg traits/concepts
	TParamVal* defaultv=0;
	TParamDef(){};
	TParamDef(SrcPos sp,Name n, TParamVal* dv,TParamVal* bound_=0, TParamVal*defaultv_=0){pos=sp;name=n;defaultv=dv;bound=bound_;defaultv=defaultv_;};
	void dump(int depth)const;
	Node* clone() const override;
	TParamDef*	as_tparam_def() override{return this;}
	const char* kind_str()const{return "TParamDef";}
};

/// CaptureVars of local variables for a lambda function
/// hidden entity created in resolve. compile to 'C' might roll these manually?
struct CaptureVars : ExprDef{
	Name			tyname(){return name;};
	ExprFnDef*		capture_from=0;
	ExprFnDef*		capture_by=0;
	Variable*		vars=0;
	CaptureVars*		next_of_from=0;
	ExprStructDef*	the_struct=0;
	void 			coalesce_with(CaptureVars* other);
	ExprStructDef*	get_struct();
	CgValue			compile(CodeGen& cg, Scope* outer);
	Node* clone() const override{
		dbprintf("warning todo template instatntiation of captures\n");
		return nullptr;
	};
	Type*			get_elem_type(int i);
	Name			get_elem_name(int i);
	int				get_elem_count();
};

struct TypeDef : ExprDef{ // eg type yada[T]=ptr[ptr[T]]; or C++ typedef
	const char* kind_str()const{return "typedef";}
	vector<TParamDef*> typeparams;
};

struct ExprLiteral : ExprDef {
	TypeId	type_id;
	ExprLiteral* next_of_scope=0;	// collected..
	Scope* owner_scope=0;
	int llvm_strlen;
	
	union  {int val_int; int val_uint; float val_float; void* val_ptr;bool val_bool; const char* val_str;int val_keyword;} u;
	virtual const char* kind_str()const{return"lit";}
	void dump(int depth) const;
	ExprLiteral(const SrcPos&s);
	ExprLiteral(const SrcPos&s,float f);
	ExprLiteral(const SrcPos&s,int i);
	ExprLiteral(const SrcPos&s,const char* start,int length);
	ExprLiteral(const SrcPos&s,const char* start);// take ownership
	~ExprLiteral();
	Node* clone() const;
	size_t strlen() const;
	bool is_string() const		{return type_id==T_CONST_STRING;}
	bool is_undefined()const	{return false;}
	const char* as_str()const	{return type_id==T_CONST_STRING?u.val_str:"";}
	ResolvedType resolve(Scope* scope, const Type* desired,int flags);
	void translate_typeparams(const TypeParamXlat& tpx);
	CgValue compile(CodeGen& cg, Scope* sc);
};

/// 'ArgDef' used for function arguments and struct-fields.
/// both have form ident:type=<default expr>
struct ArgDef :ExprDef{
	Scope*	owner=0;
	void set_owner(Scope* s){
		ASSERT(owner==0 ||owner==s);
		this->owner=s;}
	ExprStructDef* member_of();
	uint32_t	size_of,offset;
	//Type* type=0;
	Expr*		default_expr=0;
	//Type* get_type()const {return type;}
	//void set_type(Type* t){verify(t);type=t;}
	//Type*& type_ref(){return type;}
	ArgDef(SrcPos p,Name n, Type* t=nullptr,Expr* d=nullptr){pos=p; name=n;set_type(t);default_expr=d; owner=0;}
	void dump(int depth) const;
	const char* kind_str()const override;
	~ArgDef(){}
	Node*	clone() const override;
	Name	as_name()const				{return this->name;}
	size_t	size()const;
	size_t alignment() const;	//todo, 	size_t		alignment() const			{ return type()->alignment();}//todo, eval templates/other structs, consider pointers, ..


	void	translate_typeparams(const TypeParamXlat& tpx) override;
	ResolvedType	resolve(Scope* sc, const Type* desired, int flags) override;
	ArgDef*	as_arg_def()		{return this;}
};

struct NamedItems {		// everything defined under a name
	Scope* owner=0;
	Name		name;
	NamedItems*		next=0;
	Type*		types=0;
	ExprFnDef*	fn_defs=0;
	ExprStructDef*	structs=0; // also typedefs?

	ExprFnDef*	getByName(Name n);
//	ExprFnDef* resolve(Call* site);
	NamedItems(Name n,Scope* s){  name=n; owner=s;next=0;fn_defs=0;structs=0;types=0;}
};

enum VarKind{VkArg,Local,Global};
struct Variable : ExprDef{
	bool		on_stack=true;
	CaptureVars*	capture_in=0;	// todo: scope or capture could be unified? 'in this , or in capture ...'
	VarKind		kind;
	Scope*		owner=0;
	short capture_index;
	bool		keep_on_stack(){return on_stack||capture_in!=0;}
	Variable*	next_of_scope=0;	// TODO could these be unified, var is owned by capture or scope
	Variable*	next_of_capture=0;
	Expr*		initialize=0; // if its an argdef, we instantiate an initializer list
	Variable(SrcPos pos,Name n,VarKind k){this->pos=pos,name=n; initialize=0; owner=0;kind=k;this->set_type(0);}
	bool		is_captured()const{return this->capture_in!=nullptr;}
	Node* clone() const {
		auto v=new Variable(this->pos,name,this->kind);
		std::cout<<"clone "<<str(name)<<this<<" as "<<v<<"\n";
		v->initialize = verify_cast<Expr*>(this->initialize->clone_if());
		v->next_of_scope=0; v->set_type(this->get_type()); return v;
	}
	Variable*	as_variable() {return this;}
	const Variable*	as_variable() const {return this;}
	void dump(int depth) const;
};

/// 'Scope'-
/// scopes are created when resolving, held on some node types.
/// blocks which can add locals or named entities have them.

struct Scope {
	ExprDef*	owner_fn=0;	// TODO: eliminate this, owner might be FnDef,Struct,ExprBlock
	Expr*		node=0;
	Scope*		parent=0;
	Scope*		next=0;
	Scope*		child=0;
	Scope*		global=0;
	Scope*		capture_from=0;	// when resolving a local/lambda function, this is the owners' stack frame, look here for capturevars
	ExprLiteral*	literals=0;
	//Call* calls;
	Variable*	vars=0;
	NamedItems*	named_items=0;
	ExprFnDef*	templated_name_fns=0;// eg  fn FNAME[T,FNAME](x:T,y:T)->{...}  if the signature matches anything.. its' used. idea for implementing OOP & variants thru templates..
	// locals;
	// captures.
	const char* name()const;
private:
	Scope(){named_items=0; owner_fn=0;node=0;parent=0;next=0;child=0;vars=0;global=0;literals=0;}
public:
	Scope(Scope* p){ASSERT(p==0);named_items=0; owner_fn=0;node=0;parent=0;next=0;child=0;vars=0;global=0;literals=0;}
	void visit_calls();
	vector<TParamDef*>* get_tparams(){return owner_fn?owner_fn->get_typeparams():nullptr; // TODO: actually need to cascade them.
	}
	TParamDef*	get_typeparam_for(Type *t);
	bool		is_typeparam(Type* t) {return get_typeparam_for(t)!=nullptr;}
	ExprStructDef*	get_receiver();
	Variable*	try_capture_var(Name ident);	//sets up the capture block ptr.
	Variable*	find_fn_variable(Name ident,ExprFnDef* f);
	Variable*	get_fn_variable(Name name,ExprFnDef* f);
	Variable*	find_variable_rec(Name ident);
	Variable*	find_scope_variable(Name ident);
	Variable*	create_variable(Node* n, Name name,VarKind k);
	Variable*	get_or_create_scope_variable(Node* creator,Name name,VarKind k);
	ExprStructDef*	try_find_struct(const Type* t){return this->find_struct_sub(this,t);}
	ExprStructDef*	find_struct_of(const Expr* srcloc);
	Scope*			parent_within_fn(){if (!parent) return nullptr; if (parent->owner_fn!=this->owner_fn) return nullptr; return parent;}
	ExprStructDef*	find_struct_sub(Scope* original,const Type* t);
	ExprStructDef*	find_struct_named(Name name);
	ExprStructDef*	find_struct_named(const Node* node){return find_struct_named(node->as_name());}
	ExprStructDef*	find_struct(const Node* node);
	ExprFnDef*	find_unique_fn_named(const Node* name_node,int flags=0, const Type* fn_type=nullptr); // todo: replace with fn type.
	ExprFnDef*	find_fn(Name name,const Expr* callsite, const vector<Expr*>& args,const Type* ret_type,int flags);
	void	add_struct(ExprStructDef*);
	void	add_fn(ExprFnDef*);
	void	add_fn_def(ExprFnDef*);
	void	dump(int depth) const;
	NamedItems* find_named_items_local(Name name);
	NamedItems* get_named_items_local(Name name);
	NamedItems* find_named_items_rec(Name name);
	ExprFor*		current_loop();
private:
	void push_child(Scope* sub) { sub->owner_fn=this->owner_fn; sub->next=this->child; this->child=sub;sub->parent=this; sub->global=this->global;}
public:
	Scope* parent_or_global()const{
		if (parent) return this->parent; else if (global && global!=this) return this->global; else return nullptr;
	}
	Scope* make_inner_scope(Scope** pp_scope,ExprDef* owner,Expr* sub_owner);
};

ResolvedType resolve_make_fn_call(Expr* receiver,ExprBlock* block,Scope* scope,const Type* desired);
/// if-else expression.
struct ExprIf :  ExprFlow {
	Scope*	scope=0;
	Expr*	cond=0;
	Expr*	body=0;
	Expr*	else_block=0;
	void	dump(int depth) const;
	ExprIf(const SrcPos& s){pos=s;name=0;cond=0;body=0;else_block=0;}
	~ExprIf(){}
	Node*	clone() const;
	bool	is_undefined()const			{
		if (cond)if (cond->is_undefined()) return true;
		if (body)if (body->is_undefined()) return true;
		if (else_block)if (else_block->is_undefined()) return true;
		return false;
	}
	const char*	kind_str()const	override	{return"if";}
	ResolvedType	resolve(Scope* scope,const Type*,int flags) ;
	void	find_vars_written(Scope* s,set<Variable*>& vars ) const override;
	void	translate_typeparams(const TypeParamXlat& tpx) override;
	CgValue			compile(CodeGen& cg, Scope* sc);
	Scope*	get_scope()	override			{return this->scope;}
};

/// For-Else loop/expression. Currrently the lowest level loop construct
/// implement other styles of loop as specializations that omit init, incr etc.
struct ExprFor :  ExprFlow {
	Expr* pattern=0;
	Expr* init=0;
	Expr* cond=0;
	Expr* incr=0;
	Expr* body=0;
	Expr* else_block=0;
	Scope* scope=0;
	void dump(int depth) const;
	ExprFor(const SrcPos& s)		{pos=s;name=0;pattern=0;init=0;cond=0;incr=0;body=0;else_block=0;scope=0;}
	bool is_c_for()const			{return !pattern;}
	bool is_for_in()const			{return pattern && cond==0 && incr==0;}
	~ExprFor(){}
	const char* kind_str()const	 override	{return"for";}
	bool is_undefined()const				{return (pattern&&pattern->is_undefined())||(init &&init->is_undefined())||(cond&&cond->is_undefined())||(incr&&incr->is_undefined())||(body&& body->is_undefined())||(else_block&&else_block->is_undefined());}
	Expr* find_next_break_expr(Expr* prev=0);
	Node* clone()const;
	Scope*		get_scope()override			{return this->scope;}
	ExprFor*	as_for()override			{return this;}
	ResolvedType resolve(Scope* scope,const Type*,int flags);
	void find_vars_written(Scope* s,set<Variable*>& vars ) const override;
	void translate_typeparams(const TypeParamXlat& tpx) override;
	CgValue compile(CodeGen& cg, Scope* sc);
};

struct Call;
struct FnName;

/// 'StructDef' handles everything for struct,trait,impl,vtable class,mod/namespace,
///
/// specific types derived expose a subset as language sugar.
/// a transpiler should handle conversions eg spot a 'struct' with pure virtuals -> trait, etc.

/// TODO a Rust Enum is sugar for a struct holding constants & derived variant structs.


struct StructInitializer{ // named initializer
	ExprBlock*		si; // struct_intializer
	Scope*			sc;
	vector<int>		field_indices;
	vector<ArgDef*> field_refs;
	vector<Expr*>	value;
	void map_fields()								{resolve(nullptr,0);}//todo..seperate out}
	StructInitializer(Scope* s,ExprBlock* block)	{si=block,sc=s;};
	ResolvedType resolve(const Type* desiredType,int flags);
};

struct ExprIdent :Expr{
	// TODO: definition pointer. (ptr to field,function,struct,typedef..)
	void		dump(int depth) const;
	Node*		clone() const;
	ExprIdent()	{};
	ExprIdent(const char* s,const char* e)	{name=Name(s,e);set_type(nullptr);}
	ExprIdent(Name n,SrcPos sp)				{pos=sp;name=n;set_type(nullptr);}
	ExprIdent(SrcPos sp,Name n)				{pos=sp;name=n;set_type(nullptr);}
	const char*	kind_str()const override		{return"ident";}
	Name		as_name()const override			{return name;};
	ExprIdent*	as_ident()						{return this;}
	bool		is_function_name()const	override;
	bool		is_variable_name()const	override;
	bool		is_placeholder()const			{return name==PLACEHOLDER;}
	bool		is_undefined()const				{return is_placeholder();}
	void		translate_typeparams(const TypeParamXlat& tpx) override;
	CgValue		compile(CodeGen&cg, Scope* sc) override;
	ResolvedType	resolve(Scope* scope, const Type* desired,int flags) override;
};

struct TypeParamXlat{
	const vector<TParamDef*>& typeparams; const vector<TParamVal*>& given_types;
	TypeParamXlat();
	TypeParamXlat(	const vector<TParamDef*>& t, const vector<TParamVal*>& g):typeparams(t),given_types(g){}
	bool typeparams_all_set()const{
		for (int i=0; i<given_types.size(); i++) {
			if (given_types[i]==0) return false;
		}
		return true;
	}
	int typeparam_index(const Name& n) const;
	void dump(int depth)const;
};

struct FindFunction {
	struct Candidate{ExprFnDef* f; int score;};
	vector<Candidate> candidates;
	Name			name;
	const Expr* 	callsite;
	int 			flags;
	bool 			verbose=false;
	int				max_candidates=5;
	const vector<Expr*>& args;
	const Type* 	ret_type;
	FindFunction(Name n, const vector<Expr*>& a, const Type* r,int f):name(n),args(a),ret_type(r),flags(f){}
	
	void consider_candidate(ExprFnDef* f);
	void find_fn_sub(Expr* src);
	void find_fn_from_scopes(Scope* s,Scope* ex);
	void insert_candidate(ExprFnDef* f,int score);
	void dump();
};









