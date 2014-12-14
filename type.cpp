#include "semantics.h"
#include "codegen.h"
#include "repl.h"
#include "lexer.h"
#include "parser.h"
#include "run_test.h"
#include "error.h"
#include "type.h"

void Type::verify(){
	verify_type(this);
	for (auto x=this->sub; x;x=x->next)
		x->verify();
}

Type* Type::get_elem(int index){
	if (this->struct_def())
		return this->struct_def()->get_elem_type(index);
	ASSERT(index>=0);
	auto s=sub;
	for (;s&&index>0;s=s->next,--index){};
	ASSERT(index==0);
	return s;
}


bool type_is_coercible(const Type* from,const Type* to,bool coerce){
	// void pointers auto-coerce like they should,
	// thats what they're there for, legacy C
	// modern code just doesn't use void*
	
	if (from->is_pointer_not_ref() && to->is_pointer_not_ref() && coerce){
		if (from->is_void_ptr() || to->is_void_ptr())
			return true;
	}
	// coercible struct-pointers with inheritance
	auto s1=from->struct_def_noderef();
	auto s2= to->struct_def_noderef();
	if (s1&&s2 && coerce){
		if (!s1->has_base_class(s2))
			return true;
	}
	
	// TODO: 'intersection type' component coercion ?
	// int coercions
	// float
	
	if ((from->is_pointer_not_ref() || from->is_int()) && to->is_bool())
		return true;
	if (to->size() >= from->size()) {
		if (from->is_float() && to->is_float()){
			return true;
		}
		if (from->is_int() && to->is_int()){
			if (from->is_signed() && to->is_signed())
				return true;
			if (!(from->is_signed() || to->is_signed()))
				return true;
		}
	}
	if (to->size() > from->size()) {
		if (from->is_int() && to->is_int())
			return true;
	}
	return false;
}


bool  Type::has_typeparam(Scope* sc){
	if (this->def) {if (this->def->as_tparam_def()) return true;}
	else
	{
		if (auto tpd=sc->get_typeparam_for(this)){
			this->set_def(tpd);
			return true;
		}
	}

	for (auto s=this->sub; s; s=s->next){
		if (s->has_typeparam(sc))
			return true;
	}
	return false;
}
ResolvedType Type::resolve(Scope* sc,const Type* desired,int flags)
{
	if(!this)return ResolvedType();
	if (!this->struct_def() && this->name>=IDENT && !is_number(this->name)){
		if (!strcmp(str(name),"Union")){
			sc->owner_fn->dump_if(0);
			this->dump(-1);newline(0);
		}
		if (!this->has_typeparam(sc)){
			if (auto sd=sc->find_struct_named(this->name)){
				this->set_struct_def(sd->get_instance(sc,this));
				dbg_instancing("found struct %s in %s ins%p on t %p\n",this->name_str(), sc->name(),this->struct_def(),this);
			}else{
				dbg_instancing("failed to find struct %s in %s\n",this->name_str(), sc->name());
#if DEBUG >=2
				sd=sc->find_struct_named(this->name);
				sc->dump(0);
#endif
			}
		}
	}
#if DEBUG >=2
	dbprintf("%s structdef=%p def= %p\n",this->name_str(),this->struct_def(),this->def);
#endif
	auto ds=desired?desired->sub:nullptr;
	for (auto s=this->sub;s;s=s->next,ds=ds?ds->next:nullptr)
		s->resolve(sc,ds,flags);
	
	return ResolvedType(this,ResolvedType::COMPLETE);
}

Type::Type(Name i,SrcPos sp){
	pos=sp;
	sub=0;
	next=0;
	name=i; //todo: resolve-type should happen here.
}
Type::Type(Node* origin,Name i){
	this->set_origin(origin);
	sub=0;
	next=0;
	name=i; //todo: resolve-type should happen here.
}

ExprStructDef*	Type::struct_def(){
	return this->def?this->def->as_struct_def():nullptr;
}
ExprStructDef*	Type::struct_def() const{
	return const_cast<Type*>(this)->def?this->def->as_struct_def():nullptr;
}

ExprStructDef* Type::struct_def_noderef()const { // without autoderef
//	if (struct_def) return struct_def->as_struct_def();
//	else return nullptr;
	return struct_def();
};

bool Type::is_equal(const Type* other,bool coerce) const{
	/// TODO factor out common logic, is_coercible(),eq(),eq(,xlat)
	if ((!this) && (!other)) return true;
	// if its' auto[...] match contents; if its plain auto, match anything.
	if (this&&this->name==AUTO){
		if (this->sub && other) return this->sub->is_equal(other->sub,coerce);
		else return true;
	}
	if (other && other->name==AUTO){
		if (other->sub && this) return other->sub->is_equal(this->sub,coerce);
		else return true;
	}
	if (!(this && other)) return false;
	
	if (type_is_coercible(this,other,coerce))
		return true;
	else
		if (this->name!=other->name)return false;

//	if (!this->sub && other->sub)) return true;
	if (other->name==STR && type_compare(this,PTR,CHAR)) return true;
	if (this->name==STR && type_compare(other,PTR,CHAR)) return true;
	
	auto p=this->sub,o=other->sub;
		
	for (; p && o; p=p->next,o=o->next) {
		if (!p->is_equal(o,coerce)) return false;
	}
	if (o || p) return false; // didnt reach both..
	return true;
}
bool Type::is_equal(const Type* other,const TypeParamXlat& xlat) const{
	if ((!this) && (!other)) return true;
	// if its' auto[...] match contents; if its plain auto, match anything.
	if (this &&this->name==AUTO){
		if (this->sub && other) return this->sub->is_equal(other->sub,xlat);
		else return true;
	}
	if (other && other->name==AUTO){
		if (other->sub && this) return other->sub->is_equal(this->sub,xlat);
		else return true;
	}
	if (!(this && other))
		return false;

	// TODO: might be more subtle than this for HKT
	auto ti=xlat.typeparam_index(other->name);
	dbg_type("%s %s\n",str(this->name),str(other->name));
	if (ti>=0){
		return this->is_equal(xlat.given_types[ti],xlat);
	}
	ti=xlat.typeparam_index(this->name);
	if (ti>=0){
		return xlat.given_types[ti]->is_equal(other,xlat);
	}

	if (this->name!=other->name)return false;
	//	if (!this->sub && other->sub)) return true;
	if (other->name==STR && type_compare(this,PTR,CHAR)) return true;
	if (this->name==STR && type_compare(other,PTR,CHAR)) return true;
	
	if (type_is_coercible(this,other,true))
		return true;

	auto p=this->sub,o=other->sub;
	
	for (; p && o; p=p->next,o=o->next) {
		if (!p->is_equal(o,xlat)) return false;
	}
	if (o || p) return false; // didnt reach both..
	return true;
}

void Type::dump_sub(int flags)const{
	if (!this) return;
	if (this->name==TUPLE) {
		dbprintf("(");
		for (auto t=sub; t; t=t->next){
			t->dump_sub(flags);
			if(t->next)dbprintf(",");
		};
		dbprintf(")");
	} else{
		dbprintf("%s",getString(name));
#if DEBUG>=2
		if (this->struct_def())
			dbprintf("( struct_def=%s )", str(this->struct_def()->get_mangled_name()));
		if (this->def)
			dbprintf("( def=%s )", str(this->def->get_mangled_name()));
#endif
		if (sub){
			dbprintf("[");
			for (auto t=sub; t; t=t->next){
				t->dump_sub(flags);
				if(t->next)dbprintf(",");
			};
			dbprintf("]");
		}
	}
}
const char* Type::kind_str()const{return"type";}

bool Type::is_complex()const{
	if (sub) return true;	// todo: we assume anything with typeparams is a struct, it might just be calculation
	for (auto a=sub; a;a=a->next)if (a->is_complex()) return true;
	if (this->is_struct()||this->name==ARRAY||this->name==VARIANT) return true;
	return false;
}
// todo table of each 'intrinsic type', and pointer to it
Type* g_bool,*g_void,*g_void_ptr,*g_int;
Type* Type::get_bool(){
	/// todo type hash on inbuilt indices
	if (g_bool)return g_bool;
	return (g_bool=new Type(nullptr,BOOL));
}
Type* Type::get_void(){
	if (g_void)return g_void;
	return (g_void=new Type(nullptr,VOID));
}
Type* Type::get_int(){
	if (g_int)return g_int;
	return (g_int=new Type(nullptr,INT));
}
Type* Type::get_void_ptr(){
	if (g_void_ptr)return g_void_ptr;
	return (g_void_ptr=new Type(nullptr,PTR,VOID));
}

bool Type::is_struct()const{
	return struct_def()!=0 || name>=IDENT; //TODO .. it might be a typedef.
}
int Type::num_pointers() const {
	if (!this) return 0;
	if (this->name==PTR || this->name==REF)
		return 1+this->sub->is_pointer();
	else return 0;
}
int Type::num_pointers_and_arrays() const {
	if (!this) return 0;
	if (this->name==PTR || this->name==REF|| this->name==ARRAY)
		return 1+this->sub->is_pointer();
	else return 0;
}
size_t Type::alignment() const{
	if (this->raw_type_flags()){
		return this->size();
	}
	size_t align=0;
	for (auto s=this->sub;s;s=s->next){
		if (auto sz=s->size()>align){align=sz;};
	}
	if (this->struct_def()){
		return this->struct_def()->alignment();
	}
	return align?align:4;
}

size_t Type::size() const{
	auto tf=this->raw_type_flags();
	if (tf){return tf&RT_SIZEMASK};
	auto union_size=[](const Type *t){
		size_t max_elem_size=0;
		for (auto s=t->sub; s;s=s->next){
			auto sz=s->size();
			if (sz>max_elem_size)max_elem_size=sz;
		}
		return max_elem_size;
	};
	if (this->name==UNION){
		return union_size(this);
	}
	if (this->name==VARIANT){
		auto align=this->alignment();
		return align+union_size(this);
	}
	if (this->name==TUPLE){
		int size=0;
		for (auto s=this->sub; s;s=s->next){
			size+=s->size();
		}
		return size;
	}
	if (this->struct_def()){
		return struct_def()->size();
	}
	return 0;
}

ExprStructDef* Type::get_struct_autoderef()const{
	auto p=this;
//	while (p && !p->is_struct()){
	while (p->is_qualifier_or_ptr_or_ref()){
		p=p->sub;
	}
	return p->struct_def();
}
ExprStructDef* Type::get_receiver()const
{
	if (this->sub)
		if (this->sub->next)
			if (this->sub->next->next)
				return this->sub->next->next->struct_def();
	return nullptr;
}

void Type::dump(int depth)const{
	if (!this) return;
	newline(depth);dump_sub(depth);
}
Type::Type(ExprStructDef* sd)
{	set_struct_def(sd); name=sd->name; sub=0; next=0;
}
Type::Type(Name outer_name,ExprStructDef* sd)
{
	name=outer_name;
	push_back(new Type(sd));
}

Node*
Type::clone() const{
	if (!this) return nullptr;
	auto r= new Type(this->get_origin(),this->name);
	r->set_struct_def(this->struct_def());
	auto *src=this->sub;
	Type** p=&r->sub;
	while (src) {
		*p= (Type*)src->clone();
		p=&((*p)->next);
		src=src->next;
	}
	return r;
}

bool Type::has_non_instanced_typeparams()const{ if (!def) return true; if (def->as_tparam_def()) return false; return true;}

void Type::translate_typeparams(const TypeParamXlat& tpx){
	this->translate_typeparams_sub(tpx,nullptr);
}
void Type::translate_typeparams_sub(const TypeParamXlat& tpx,Type* inherit_replace){
	// todo: replace wih 'instantiate' typparams, given complex cases
	/*
	 example:
	 struct vector<T,N=int> {
 	data:*T;
 	count:N;
	 }
	 
	 instantiate vector<string,short>
	 translate_tempalte_type( {T,N}, {string,short}, *T) -> *string
	 translate_tempalte_type( {T,N}, {string,short}, N) -> short.
	 
	 HKT example
	 struct tree<S,T>{
 	S<tree<S,T>>	sub;
	 }
	 instantiate tree<vector,int>
	 we want:
	 struct tree {
	 vector< tree<vector,int>> sub;
	 }
	 
	 translate_tempalte_type( {S,T}, {vector,int}, S<tree<S,T>>)->    vector<tree<vector<int>>> //
	 
	 */
	// TODO: assert there is no shadowing in this types' own definitions
	
	Type* new_type=0;
	this->clear_struct_def();
	int param_index=tpx.typeparam_index(this->name);
	if ((this->name==PLACEHOLDER || this->name==AUTO) && inherit_replace){
		this->name=inherit_replace->name;
		if (!this->sub && inherit_replace->sub){
			error(this,"TODO - replace an auto typeparam with complex given typeparam ");
		}
	}
	if (param_index>=0){
		auto pi=param_index;
		auto src_ty=tpx.given_types[pi];
		if (!src_ty){error(this,"typaram not given,partial instance?");}
		if (!src_ty->sub) {
			this->name=src_ty->name;
		} else if (!this->sub){
			this->name=src_ty->name;
			for (auto s=src_ty->sub;s;s=s->next){
				this->push_back((Type*)s->clone());
			};
		}
		else {
#if DEBUG>=2
			tpx.dump(0);
			tpx.typeparams[pi]->dump(-1);
			dbg_generic(" replace with ");
			tpx.given_types[pi]->dump(-1);newline(0);
			newline(0);
			dbg_generic("substituting in:");
			this->dump(-1);
			newline(0);
#endif
			//error_begin(this,"param index %d %s trying to instantiate complex typeparameter into non-root of another complex typeparameter,we dont support this yet\n",param_index, tpx.typeparams[pi]->name_str());
			
			//error_end(this);
			this->name=src_ty->name;
			auto inherit_sub=inherit_replace?inherit_replace->sub:nullptr;
			auto* pps=&this->sub;
			for (auto s=this->sub; s; pps=&s,s=s->next){
				s->translate_typeparams_sub(tpx,inherit_sub);
				inherit_sub=inherit_sub?inherit_sub->next:nullptr;
			}
			
#if DEBUG>=2
			dbg_generic("result:");
			this->dump(-1);
			newline(0);
#endif
		}
	}
	//this->struct_def=tpx.
	// translate child elems.
	auto inherit_sub=inherit_replace?inherit_replace->sub:nullptr;
	auto* pps=&this->sub;
	for (auto sub=this->sub; sub; pps=&sub->next,sub=*pps) {
		sub->translate_typeparams_sub(tpx,inherit_sub);
		inherit_sub=inherit_sub?inherit_sub->next:nullptr;
	}
	// replace any not give with inherit..
	while (inherit_sub){
		*pps = (Type*)inherit_sub->clone();
		inherit_sub=inherit_sub->next;
		pps=&((*pps)->next);
	}
}

void Type::clear_struct_def(){
	this->clear_def();
}
void Type::set_struct_def(ExprStructDef* sd){
	this->set_def(sd);
}
void Type::push_back(Type* t) {
	if (!sub) sub=t;
	else {
		auto s=sub;
		for (; s->next!=0; s=s->next){};
		s->next =t;
	}
}



// todo table of each 'intrinsic type', and pointer to it