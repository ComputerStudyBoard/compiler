#include "ast.h"
#include "type.h"
#include "scope.h"
#include "semantics.h"
#include "exprstructdef.h"
#include "codegen.h"
#include "assist.h"

void Pattern::recurse(std::function<void(Node*)>& f){
	if (!this) return;
	for (auto p=sub;p;p=p->next)
		p->recurse(f);
}
Node*Pattern::clone() const {
	auto np=new Pattern(pos,name);
	np->next=(Pattern*)next->clone_if();// todo not recursive!! .. but patterns are small.
	np->sub=(Pattern*)sub->clone_if();
	return np;
}
Pattern* Pattern::get_elem(int i){
	auto s=sub;
	for (; s && i>0; s=s->next,i--){}
	return s;
}
const Pattern* Pattern::get_elem(int i) const{
	auto s=sub;
	for (; s && i>0; s=s->next,i--){}
	return s;
}
Name Pattern::as_name()const{
	return this->name;
}
ResolvedType
Pattern::resolve(Scope* sc, const Type* rhs, int flags){
	return this->resolve_with_type(sc,rhs,flags);
}

ResolvedType
Pattern::resolve_with_type(Scope* sc, const Type* rhs, int flags){
	if (!this)
		return ResolvedType();
	if (this->name==EXPRESSION){
		((Node*)(this->sub))->resolve(sc,rhs,flags);
		return propogate_type(flags,(Node*)this, this->type_ref(), this->sub->type_ref());
	}
	if (this->name==PTR){
		if (rhs&&rhs->name==PTR){
			auto ret=sub->resolve_with_type(sc, rhs->sub, flags);
			this->set_type(new Type(this,PTR,sub->type()));
			return ret;
		}
	}
	if (this->name==OR){
		for (auto s=this->sub;s;s=s->next){
			s->resolve_with_type(sc,rhs,flags);
		}
		if (rhs)
			return propogate_type_fwd(flags, (Node*)this, rhs, this->type_ref());
		else
			return ResolvedType();
	} else if (this->name==PATTERN_BIND){
		// get or create var here
		auto v=this->sub; auto p=v->next; ASSERT(p);
		p->resolve_with_type(sc,rhs,flags);
		if (!v->type()&&p->type()){
			v->set_type((Type*)(p->type()->clone()));
		}
		v->resolve_with_type(sc,p->type(),flags);
		return propogate_type(flags, (Node*)this, this->sub->type_ref(), this->sub->next->type_ref());
	}
	else if (this->name==TUPLE){
		auto subt=rhs?rhs->sub:nullptr;
		for (auto subp=this->sub; subp; subp=subp->next, subt?subt=subt->next:nullptr){
			subp->resolve_with_type(sc,subt, flags);
		}
	} else if (this->name!=TUPLE && this->sub){ // Type(..,..,..) destructuring
		auto sd=sc->find_struct_type(this,rhs);// todo tparams from rhs, if given
		if (sd){
			int i=sd->first_user_field_index(); auto subp=this->sub;
			// todo - sub types should resolve?!
			for (; i<sd->fields.size() && subp; i++,subp=subp->next){
				auto ft=sd->fields[i]->type();
				subp->set_type(ft);
				subp->resolve_with_type(sc,ft,flags);
			}
			this->set_struct_type(sd);
		}
	} // else its a var of given type, or just a constant?
	else{
		if (rhs)
			propogate_type_fwd(flags, (Node*)this, rhs,this->type_ref());
		dbg(dbprintf("matching pattern %s with..",str(this->name)));dbg(rhs->dump_if(-1));dbg(newline(0));
		if (auto sd=sc->find_struct_name_type_if(sc,this->name,this->type()))
		{
			this->set_struct_type(sd);
			return ResolvedType();
		}
		if (auto sd=sc->find_struct_named(this->name)){
			this->set_struct_type(sd);
			return ResolvedType();
		}
		if (auto sd=sc->find_inner_def_named(sc,this, 0)){

			this->set_struct_type(sd);
			return ResolvedType();
		}

		// TODO named-constants
		// TODO boolean true/false
		// TODO nullptr
		if (is_number(this->name)){
			if (!this->type()) {this->set_type(Type::get_int());}
			return ResolvedType();
		}
		else
		if (this->name==PLACEHOLDER){
			this->set_type(rhs);
			return ResolvedType();
			
		} else {
			// TODO - scala style quoted variable for comparison
			auto v=sc->create_variable(this,this->name,Local);
			if (!this->def){
				dbg2(dbprintf("pattern match created var %s:",this->name_str())); dbg2(this->type()->dump_if(-1));dbg(newline(0));
				this->set_def(v);
			}
			if (rhs)
				return propogate_type_fwd(flags, this, rhs, v->type_ref());
			else
				return ResolvedType();
		}
	}
	return ResolvedType();
}

// TODO: we suspect this will be more complex, like Type translation (
void Pattern::translate_typeparams(const TypeParamXlat& tpx){
	this->type()->translate_typeparams_if(tpx);
	this->def->translate_typeparams_if(tpx);
	auto i=tpx.typeparam_index(this->name);
	if (i>=0){
		this->name=tpx.given_types[i]->name;
	}
	for (auto s=this->sub; s;s=s->next){
		s->translate_typeparams(tpx);
	}
}

CgValue Pattern::compile(CodeGen &cg, Scope *sc, CgValue val){
	auto ptn=this;
	// emit a condition to check if the runtime value  fits this pattern.
	// TODO-short-curcuiting - requires flow JumpToElse.
	dbg(val.dump());
	// single variable bind.
	if (ptn->name==EXPRESSION){
		auto rhs=ptn->sub->compile(cg,sc,CgValue());
		if (val.is_valid()){
			return cg.emit_instruction(EQ, rhs, val);
		} else
			return rhs;
	}
	if (ptn->name==PTR ||ptn->name==REF){
		return ptn->sub->compile(cg, sc, val.is_valid()?val.deref_op(cg):val);
	}
	if (ptn->name==PLACEHOLDER){
		return cg.emit_val_bool(true);
	}
	if (ptn->name==PATTERN_BIND){
		auto v=ptn->get_elem(0);
		auto p=ptn->get_elem(1);
		auto disr=val.is_valid()?cg.emit_loadelement(val, __DISCRIMINANT):val;
		auto ps=p->type()->is_pointer_or_ref()?p->sub:p;
		auto sd=ps->def->as_struct_def();
		auto b=cg.emit_instruction(EQ, disr, cg.emit_val_i32(sd->discriminant));
		auto var=v->def->as_variable();
		if (val.is_valid())
			CgValue(var).store(cg, cg.emit_conversion((Node*)ptn, val, var->type(), sc));// coercion?
		
		return b;
	}
	else if (ptn->name==RANGE || ptn->name==RANGE_LT){
		auto sp=ptn->sub;
		auto lo=sp->compile(cg,sc,CgValue());
		auto hi=sp->next->compile(cg,sc,CgValue());
		return	cg.emit_instruction(
									AND,Type::get_bool(),
									cg.emit_instruction(GE,val,lo),
									cg.emit_instruction(ptn->name==RANGE?LE:LT,val,hi)
									);
	}
	else
	if (ptn->name==OR || ptn->name==TUPLE ||ptn->sub){// iterate components...
		int index;
		CgValue ret=CgValue();
		CgValue	val2=val;
		Name op;
		if (ptn->name==OR) {op=LOG_OR;index=0;}
		else if(ptn->name==TUPLE){op=LOG_AND;index=0;}
		else{
			auto disr=cg.emit_loadelement(val, __DISCRIMINANT);
			auto ptns=ptn->type()->is_pointer_or_ref()?ptn->sub:ptn;
			auto sd=ptns->def->as_struct_def();
			index=sd->first_user_field_index();
			ret=cg.emit_instruction(EQ, disr, cg.emit_val_i32(sd->discriminant));
			if (val.is_valid())
				val2=cg.emit_conversion((Node*)ptn,val, ptn->type(),sc);
			dbg2(val2.type->dump(0));dbg2(newline(0));
		}
		//todo - this part moves to bind if not or/tuple
		for (auto subp=ptn->sub; subp; subp=subp->next,index++){
			auto elem=ptn->name!=OR?cg.emit_getelementref(val2,0, index,subp->type()):val;
			auto b=subp->compile(cg, sc, elem);
			if (ptn->name==OR || ptn->name==TUPLE){
				//ASSERT(b.type->is_bool())
			}
			if (op){
				if (!ret.is_valid())
					ret=b;
				else
					ret=cg.emit_instruction(op,Type::get_bool(),ret,b);
			}
		}
		dbg(ret.dump());
		return ret;
	}
	else if (ptn->def){
		if (auto var=ptn->def->as_variable()){
			auto varr=var->compile(cg,sc,CgValue());
			
			

			
			dbg(dbprintf("bind %s :",var->name_str()));dbg(var->type()->dump_if(-1));dbg(newline(0));
			dbg(dbprintf("given val :",var->name_str()));dbg(val.type->dump_if(-1));dbg(newline(0));dbg(ptn->type()->dump_if(-1));dbg(val.dump());dbg(newline(0));
			if (val.is_valid())
				varr.store(cg, val);
			return cg.emit_val_bool(true);
		}else
			// single value
			if (auto lit=ptn->def->as_literal()){
				return	cg.emit_instruction(EQ, CgValue(lit), val);
			}
	}
	if (is_number(this->name)){
	// its a constant or number
		auto cmpval=cg.emit_val_i32(getNumberInt(this->name));
		if (val.is_valid()){
			return	cg.emit_instruction(EQ, cmpval, val);
		}else
			return cmpval;
	}
	dbg(ptn->dump(0));
	dbg(dbprintf("uncompiled node %s\n",str(ptn->name)));
	return CgValue();
}

void Pattern::push_back(Pattern* newp){
	if (!newp) return;
	Pattern **pp=&sub;
	while (auto p=*pp){pp=&p->next;}
	*pp=newp;
}
void Pattern::push_child(Pattern* newp) {
	if (!newp) return;
	auto p=this;
	for (; p->sub; p=p->sub){};
	p->sub=newp;
}
void Pattern::dump(int depth)const{
	int d2=depth>=0?depth+1:depth;
	newline(depth);
	if (name==TUPLE){
	}else
	if (name==REF){
		dbprintf("&");
	}else if (name==PTR){
		dbprintf("*");
	}else if (name==OR){
		for (auto s=this->sub;s;s=s->next){
			s->dump(-1);
			if (s->next)dbprintf("|");
		}
		return;
	} else if (name==PATTERN_BIND){
		sub->dump(-1);dbprintf("@");
		sub->next->dump_if(-1);
	} else if(name==RANGE || name==RANGE_LT||name==RANGE_LE){
		sub->dump(-1);dbprintf("..");sub->next->dump(-1);
	}else
		dbprintf(str(name));
	if (this->sub && name!=PATTERN_BIND && name!=RANGE){
		dbprintf("(");
		for (auto s=this->sub;s;s=s->next){
			s->dump(d2);if (s->next)dbprintf(",");
		}
		dbprintf(")");
	}
	if (this->type()) {dbprintf(":"); this->type()->dump_if(-1);dbprintf(" ");}
}
ResolvedType ExprIdent::resolve(Scope* scope,const Type* desired,int flags) {
	// todo: not if its' a typename,argname?
	if (this->is_placeholder()) {
		//PLACEHOLDER type can be anything asked by environment, but can't be compiled .
		propogate_type_fwd(flags,this, desired,this->type_ref());
		return ResolvedType(this->type_ref(),ResolvedType::COMPLETE);
	}
	
	propogate_type_fwd(flags,this, desired,this->type_ref());
	if (this->type()) this->type()->resolve(scope,desired,flags);
	if (auto sd=scope->find_struct_name_type_if(scope,this->name,this->type())) {
		this->set_def(sd);
		return propogate_type_fwd(flags,this, desired,this->type_ref());
	}else
	if (auto sd=scope->find_struct_named(this->name)) {
		this->set_def(sd);
		if (!this->get_type()){
			this->set_type(new Type(sd));
			return propogate_type_fwd(flags,this, desired,this->type_ref());
		}
	} else
		if (auto v=scope->find_variable_rec(this->name)){ // look for scope variable..
			v->on_stack|=flags&R_PUT_ON_STACK;
			this->set_def(v);
			return propogate_type(flags,this, this->type_ref(),v->type_ref());
		}
	if (auto sd=scope->get_receiver()) {
		if (auto fi=sd->try_find_field(this->name)){
			this->set_def(fi);
			// anonymous struct fields are possible in local anon structs..
			return propogate_type(flags,this, this->type_ref(),fi->type_ref());
		}
	}
	if (auto f=scope->find_unique_fn_named(this,flags)){ // todo: filter using function type, because we'd be storing it as a callback frequently..
		// TODO; loose end :( in case where arguments are known, this overrides the match
		//we eitehr need to pass in arguemnt informatino here *aswell*, or seperaete out the callsite case properly.
		this->set_def(f);
		this->set_type(f->fn_type);
		return propogate_type_fwd(flags,this, desired, this->type_ref());
	}
	else if (!scope->find_named_items_rec(this->name)){
		// from an inner scope? caution here - it must count ambiguity- give lambda..
		if (auto def=scope->find_inner_def(scope,this,this->type(),flags)){
			this->set_struct_type(def);
			return propogate_type_fwd(flags,this, desired,this->type_ref());
		}
		
		// didn't find it yet, can't do anything
		if (flags & R_FINAL){
			//			if (g_verbose){
			//				g_pRoot->as_block()->scope->dump(0);
			//			}
			scope->find_variable_rec(this->name); // look for scope variable..

			dbg2(scope->dump(0));
			
			auto thisptr=scope->find_variable_rec(THIS);
			if (thisptr){
				auto recv=scope->get_receiver();
				thisptr->dump(0);
				scope->dump(0);
				if (!recv){
					dbprintf("warning 'this' but no receiver found via scope\n");
					dbprintf("warning 'this' but no receiver in s=%p owner=%p\n",scope,scope->owner_fn);
					if (scope->owner_fn){
						dbprintf("warning 'this' but no receiver in %s %s\n",scope->owner_fn->kind_str(),scope->name());
						auto fnd=scope->owner_fn->as_fn_def();
						auto p=scope->owner_fn->parent();
						dbprintf("warning 'this' but no receiver in %s %s\n",scope->owner_fn->kind_str(),scope->name());
						
						dbprintf("recv=%p\n",fnd->m_receiver);
						scope->owner_fn->dump(0);
					}
				}
			}
			error_begin(this,"\'%s\' undeclared id",str(this->name));
			assist_find_symbol(this,scope,this->name);
			error_end(this);
		}
		return ResolvedType();
	}
	return ResolvedType();
}
void ExprIdent::dump(int depth) const {
	if (!this) return;
	newline(depth);dbprintf("%s ",getString(name));
	//	if (this->def) {dbprintf("(%s %d)",this->def->pos.line);}
	if (auto t=this->get_type()) {dbprintf(":");t->dump(-1);}
}
CgValue	ExprIdent::compile(CodeGen& cg, Scope* sc, CgValue){
	auto n=this;
	// Its' either a local, part of 'this', or in a capture...
	auto var=sc->find_variable_rec(n->name);
	if (!var){
		if (auto fi=n->def->as_field()){
			// emit Load instruction
			auto thisv=sc->find_variable_rec(THIS);
			sc->dump(0);
			ASSERT(thisv&&"attempting to find field in non method,?");
			return CgValue(thisv).get_elem(cg, this, sc);
		} else
			if (n->def) {
				return CgValue(n->def);
			}
		error(n,"var not found %s\n",n->name_str());
		return CgValue();
	}
	else if (auto cp=var->capture_in){
//		return CgValue(cp->reg_name,cp->type(),0,var->capture_index);

		return var->compile(cg,sc,CgValue());
	}
	if (var && var!=n->def){
		error(n,"var/def out of sync %s %s\n",n->name_str(),var->name_str());
		return CgValue();
	}
	return CgValue(var);
}


void	ExprDef::remove_ref(Node* ref){
	Node** pp=&refs;
	Node* p;
	auto dbg=[&](){
		for (auto r=refs; r; r=r->next_of_def){
			dbprintf("ref by %p %s %s %d\n",r, r->kind_str(), str(r->name),r->	pos.line);
		}
	};
	for (p=*pp; p; pp=&p->next_of_def, p=*pp) {
		if (p==ref) *pp=p->next_of_def;
	}
	ref->def=0;
}

CgValue	ExprLiteral::compile(CodeGen& cg, Scope* sc, CgValue ) {
	return CgValue(this);
}


void ExprLiteral::dump(int depth) const{
	if (!this) return;
	newline(depth);
	if (type_id==T_VOIDPTR){dbprintf("%p:*void",u.val_ptr);}
	if (type_id==T_BOOL){dbprintf(u.val_bool?"true":"false");}
	if (type_id==T_VOID){dbprintf("void");}
	if (type_id==T_INT){dbprintf("%d",u.val_int);}
	if (type_id==T_FLOAT){dbprintf("%.7f",u.val_float);}
	if (type_id==T_KEYWORD){dbprintf("%s",str(u.val_keyword));}
	if (type_id==T_CONST_STRING){
		dbprintf("\"%s\"",u.val_str);
	}
	if (auto t=this->type()){
		dbprintf(":");t->dump(-1);
	}
}
// TODO : 'type==type' in our type-engine
//	then we can just make function expressions for types.

void	ExprLiteral::translate_typeparams(const TypeParamXlat& tpx){
	
}

ResolvedType ExprLiteral::resolve(Scope* sc , const Type* desired,int flags){
	if (!this->owner_scope){
		this->next_of_scope=sc->global->literals;
		sc->global->literals=this;
		this->owner_scope=sc->global;
	}
	if (this->name==0){
		char str[256]; if (this->name==0){sprintf(str,"str%x",(uint)(size_t)this); this->name=getStringIndex(str);}
	}
	if (!this->get_type()) {
		Type* t=nullptr;
		switch (type_id) {
			case T_VOID: t=new Type(this,VOID); break;
			case T_INT: t=new Type(this,INT); break;
			case T_ZERO: t=new Type(this,ZERO); break;
			case T_ONE: t=new Type(this,ONE); break;
			case T_FLOAT: t=new Type(this,FLOAT); break;
			case T_CONST_STRING: t=new Type(this,STR); break;
			default: break;
		}
		this->set_type(t); // one time resolve event.
	}
	return propogate_type_fwd(flags,this, desired,this->type_ref());
}
size_t ExprLiteral::strlen() const{
	if (type_id==T_CONST_STRING)
		return ::strlen(this->u.val_str);
	else return 0;
}
ExprLiteral::ExprLiteral(bool b) {
	set_type(new Type(this,BOOL));
	type_id=T_BOOL;
	u.val_bool=b;
}

ExprLiteral::ExprLiteral(const SrcPos& s) {
	pos=s;
	this->owner_scope=0;
	set_type(new Type(this,VOID));
	type_id=T_VOID;
}

ExprLiteral::ExprLiteral(const SrcPos& s,float f) {
	pos=s;
	this->owner_scope=0;
	set_type(new Type(this,FLOAT));
	type_id=T_FLOAT;
	u.val_float=f;
	this->name = getNumberIndex(f);
}
ExprLiteral::ExprLiteral(const SrcPos& s,int i) {
	pos=s;
	this->owner_scope=0;
	set_type(new Type(this,INT));
	type_id=T_INT;
	u.val_int=i;
	this->name = getNumberIndex(i);
}
ExprLiteral::ExprLiteral(const SrcPos& s,const char* start,int length) {//copy
	pos=s;
	this->owner_scope=0;
	set_type(new Type(this,STR));
	type_id=T_CONST_STRING;
	auto str=( char*)malloc(length+1); ;
	u.val_str=str;memcpy(str,(void*)start,length);
	str[length]=0;
}
ExprLiteral::ExprLiteral(const SrcPos& s,const char* src) {//take ownership
	pos=s;
	this->owner_scope=0;
	set_type(new Type(this,STR));
	u.val_str=src;
	type_id=T_CONST_STRING;
}
ExprLiteral::~ExprLiteral(){
	if (type_id==T_CONST_STRING) {
		free((void*)u.val_str);
	}
}
/*if (auto cp=var->capture_in){
	varr=CgValue(cp->reg_name,cp->type(),0,var->capture_index);
} else
varr=CgValue(var);

return CgValue(cp->reg_name,cp->type(),0,var->capture_index);
 */

CgValue Variable::compile(CodeGen& cg, Scope* sc, CgValue input){
	if (auto cp=this->capture_in){
		return CgValue(cp->reg_name,cp->type(),0,this->capture_index);
	} else
		return CgValue(this);
}

void Variable::dump(int depth) const{
	newline(depth);dbprintf("%s",getString(name));
	if (type()) {dbprintf(":");type()->dump(-1);}
	switch (this->kind){
		case VkArg:dbprintf("(Arg)");break;
		case Local:dbprintf("(%s Local)",this->on_stack?"stack":"reg");break;
		case Global:dbprintf("(Local)");break;
		default: break;
	}
}

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
//	if (this->pattern)
//		this->pattern->resolve(sc,this->type(),flags);
	if (this->default_expr){this->default_expr->resolve(sc,this->type(),flags);}
	return ResolvedType(this->type(), ResolvedType::COMPLETE);
}
void ArgDef::recurse(std::function<void(Node*)>&f){
	this->type()->recurse(f);
	this->pattern->recurse(f);
	this->default_expr->recurse(f);
}

void ArgDef::dump(int depth) const {
	newline(depth);dbprintf("%s",getString(name));
	this->pattern->dump_if(-1);
	if (this->type()) {dbprintf(":");type()->dump(-1);}
	if (default_expr) {dbprintf("=");default_expr->dump(-1);}
}

Node*	TParamDef::clone() const
{	return new TParamDef(this->pos,this->name, (TParamVal*) (this->bound->clone_if()),(TParamVal*) (this->defaultv->clone_if()));
}

void TParamDef::dump(int depth) const {
	newline(depth);dbprintf("%s",str(name));
	if (defaultv) {dbprintf("=");defaultv->dump(-1);}
}
const char* ArgDef::kind_str()const{return"arg_def";}

// the operators should all just be functioncalls, really.
// return type of function definition is of course a function object.
// if we make these things inline, we create Lambdas
// todo: receivers.
Node*
ExprLiteral::clone() const{
	return (Node*)this;	// TODO - ensure this doesn't get into dangling state or anything!
	/*	if (!this) return nullptr;
	 auto r=new ExprLiteral(0); if (this->is_string()){r->u.val_str=strdup(this->u.val_str);}else r->u=this->u;
	 r->type_id=this->type_id;
	 r->llvm_strlen=this->llvm_strlen; // TODO this should just be a reference!?
	 r->name=this->name;
	 return r;
	 */
}

Node*
ArgDef::clone() const{
	if (!this) return nullptr;
	auto ad=new ArgDef(this->pos,this->name, (Type*)this->type()->clone_if(),(Expr*)this->default_expr->clone_if());
	ad->pattern=(Pattern*)this->pattern->clone_if();
	return ad;
}

Node*
ExprIdent::clone() const {
	auto r=new ExprIdent(this->name,this->pos);
	r->set_type(this->get_type());	// this is where given typeparams live.
	r->clear_def();	// def will need re-resolving.
	return r;
}

void
ExprIdent::recurse(std::function<void(Node*)>&f){
	if (!this)return;
	// typeparams?
	type()->recurse(f);
}

IdentWithTParams::IdentWithTParams(SrcPos _pos, ExprIdent* id) {
	name=id->name;
	this->ident=id;
	this->pos=_pos;
}
Type* IdentWithTParams::make_type(Scope* sc)const{
	auto t=new Type(this->ident->as_name(),pos);
	for (auto x:given_tparams)t->push_back((TParamVal*)x->clone());
	return t;
}
int IdentWithTParams::get_elem_count()const{
	return this->given_tparams.size();
}
Node* IdentWithTParams::get_elem_node(int i){
	return this->given_tparams[i];
}
void IdentWithTParams::dump(int depth) const{
	newline(depth);ident->dump(-1);
	dbprintf("<");
	for (auto x:given_tparams){x->dump(-1);dbprintf(",");}
	dbprintf(">");
}
void IdentWithTParams::recurse(std::function<void(Node*)>& f) {
	ident->recurse(f);
	for (auto x:given_tparams){
		x->recurse(f);
	}
	type()->recurse(f);
}
void IdentWithTParams::translate_typeparams(const TypeParamXlat& tpx) {
	// templated idents?
	// TODO - these could be expresions - "concat[T,X]"
	this->name.translate_typeparams(tpx);
	for (auto x:given_tparams){x->translate_typeparams(tpx);}
	this->type()->translate_typeparams_if(tpx);
}

Node* IdentWithTParams::clone()const {
	auto iwt=new IdentWithTParams(pos,(ExprIdent*)ident->clone());
	iwt->given_tparams.reserve(given_tparams.size());
	for (auto x:given_tparams){
		iwt->given_tparams.push_back((TParamVal*)x->clone());
	}
	return (Node*) iwt;
}

void Name::translate_typeparams(const TypeParamXlat& tpx) {
	auto index=tpx.typeparam_index(*this);
	if (index>=0){
		ASSERT(tpx.given_types[index]->sub==0 && "TODO type-expressions for name? concat[],...");
		*this=tpx.given_types[index]->name;
	}
}
void ExprIdent::translate_typeparams(const TypeParamXlat& tpx) {
	// templated idents?
	// TODO - these could be expresions - "concat[T,X]"
	this->name.translate_typeparams(tpx);
	this->type()->translate_typeparams_if(tpx);
}

ExprDef* ArgDef::
member_of()	{ // todo, implement for 'Variable' aswell, unify capture & member-object.
	if (owner)
		return (ExprDef*)owner->get_receiver();
	return nullptr;
}

void ArgDef::translate_typeparams(const TypeParamXlat& tpx){
	this->name.translate_typeparams(tpx);
	if (this->get_type()){
		if (!this->get_type()->is_anon_struct())
			this->get_type()->set_struct_def(nullptr); // needs resolving again
		this->get_type()->translate_typeparams(tpx);
	}
	if (this->default_expr){
		this->default_expr->translate_typeparams(tpx);
	}
}

const Type* CaptureVars::get_elem_type(int i)const {
	auto v=vars;
	for (; v&&i>0; i--,v=v->next_of_capture);
	return v->type();
}

Name CaptureVars::get_elem_name(int i)const {
	auto v=vars;
	for (; v&&i>0; i--,v=v->next_of_capture);
	return v->name;
}
int CaptureVars::get_elem_count()const {
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


void commit_capture_vars_to_stack(CodeGen& cg, CaptureVars* cp){
	if (!cp) return;
	return;
}
void	CaptureVars::recurse(std::function<void(Node*)>&) {
	//TODO - not sure if this makes sense.
	//CaptureVars is not part of the AST. but it is compilable.
	// it contains references to variables, & links to its owner & context functions
}

CgValue CaptureVars::compile(CodeGen& cg, Scope* outer_scope, CgValue){
	auto cp=this;
	cg.emit_struct_def_begin(cp->tyname());
	decltype(cp->vars->capture_index) i=0;
	for (auto v=cp->vars;v;v=v->next_of_capture,++i){
		cg.emit_type(v->type());
		v->capture_index=i;
	}
	cg.emit_struct_def_end();
	cp->type() = new Type(cp->capture_by, PTR,cp->tyname());
	cp->type()->sub->set_struct_def((ExprStructDef*) cp);
	return CgValue(this);
}







