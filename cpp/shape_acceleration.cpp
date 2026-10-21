// Handwritten shape acceleration algorithms. Staged; not registered/installed.
#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <nanobind/stl/string.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nx { namespace {
nb::object ac_check(PyObject* value){if(!value)throw nb::python_error();return nb::steal<nb::object>(value);}
nb::object ac_attr(nb::object value,const char* name){return ac_check(PyObject_GetAttrString(value.ptr(),name));}
nb::object ac_item(nb::object value,nb::object key){return ac_check(PyObject_GetItem(value.ptr(),key.ptr()));}
bool ac_truth(nb::object value){int result=PyObject_IsTrue(value.ptr());if(result<0)throw nb::python_error();return result!=0;}
nb::object ac_iter(nb::object value){return ac_check(PyObject_GetIter(value.ptr()));}
nb::object ac_next(nb::handle value){PyObject* result=PyIter_Next(value.ptr());if(!result&&PyErr_Occurred())throw nb::python_error();return nb::steal<nb::object>(result);}
using ACExpr=std::function<nb::object()>;
using ACKw=std::pair<const char*,ACExpr>;
nb::object ac_call(nb::object fn,std::initializer_list<ACExpr> args={},std::initializer_list<ACKw> kws={}){
 nb::tuple positional=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(args.size())));if(!positional.is_valid())throw nb::python_error();Py_ssize_t i=0;
 for(const auto& arg:args){nb::object value=arg();PyTuple_SET_ITEM(positional.ptr(),i++,value.release().ptr());}
 nb::dict keywords;for(const auto& kw:kws)keywords[kw.first]=kw.second();
 return ac_check(PyObject_Call(fn.ptr(),positional.ptr(),kws.size()?keywords.ptr():nullptr));
}
nb::tuple ac_tuple(std::initializer_list<ACExpr> args){nb::tuple out=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(args.size())));if(!out.is_valid())throw nb::python_error();Py_ssize_t i=0;for(const auto& arg:args){nb::object value=arg();PyTuple_SET_ITEM(out.ptr(),i++,value.release().ptr());}return out;}
nb::list ac_list(std::initializer_list<ACExpr> args){nb::list out;for(const auto& arg:args)out.append(arg());return out;}
struct AccelerationTyping {
 nb::object context,operators,numpy,as_dtype;
 std::function<nb::object(size_t)> argument_value;
 nb::handle argument_types;
};
thread_local AccelerationTyping* ac_active_typing=nullptr;
nb::object ac_typed_binary(nb::object left,nb::object right,binaryfunc fn){
 auto& metadata=*ac_active_typing;const char* operation=nullptr;
 if(fn==PyNumber_Add||fn==PyNumber_InPlaceAdd)operation="add";
 else if(fn==PyNumber_Subtract||fn==PyNumber_InPlaceSubtract)operation="sub";
 else if(fn==PyNumber_Multiply||fn==PyNumber_InPlaceMultiply)operation="mul";
 else if(fn==PyNumber_TrueDivide||fn==PyNumber_InPlaceTrueDivide)operation="truediv";
 if(!operation)return ac_check(fn(left.ptr(),right.ptr()));
 auto type_of=[&](nb::object value){
  for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(metadata.argument_types.ptr());++i)
   if(value.ptr()==metadata.argument_value(static_cast<size_t>(i)).ptr())return nb::borrow<nb::object>(PyTuple_GET_ITEM(metadata.argument_types.ptr(),i));
  return ac_call(ac_attr(metadata.context,"resolve_value_type"),{[&]{return value;}});
 };
 nb::object left_type=type_of(left),right_type=type_of(right);
 nb::object operator_type=ac_call(ac_attr(metadata.context,"resolve_value_type"),{[&]{return ac_attr(metadata.operators,operation);}});
 nb::tuple operand_types=ac_tuple({[&]{return left_type;},[&]{return right_type;}});
 nb::object signature=ac_call(ac_attr(metadata.context,"resolve_function_type"),{[&]{return operator_type;},[&]{return operand_types;},[]{return nb::dict();}});
 if(signature.is_none())throw nb::type_error("Numba cannot type native acceleration arithmetic");
 nb::object result_type=ac_attr(signature,"return_type");
 PyObject* array_dtype=PyObject_GetAttrString(result_type.ptr(),"dtype");
 if(array_dtype){
  nb::object dtype=nb::steal<nb::object>(array_dtype);nb::object numpy_dtype=ac_call(metadata.as_dtype,{[&]{return dtype;}});
  nb::object raw=ac_check(fn(left.ptr(),right.ptr()));
  return ac_call(ac_attr(metadata.numpy,"asarray"),{[&]{return raw;}},{{"dtype",[&]{return numpy_dtype;}}});
 }
 if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw nb::python_error();PyErr_Clear();
 nb::object dtype=ac_call(metadata.as_dtype,{[&]{return result_type;}});nb::object cast=ac_attr(dtype,"type");
 left=ac_call(cast,{[&]{return left;}});right=ac_call(cast,{[&]{return right;}});
 std::string kind=nb::cast<std::string>(ac_attr(dtype,"kind"));
 if(kind=="f"){
  double a=PyFloat_AsDouble(left.ptr());if(PyErr_Occurred())throw nb::python_error();double b=PyFloat_AsDouble(right.ptr());if(PyErr_Occurred())throw nb::python_error();
  if(operation==std::string("truediv")&&b==0.0){PyErr_SetString(PyExc_ZeroDivisionError,"division by zero");throw nb::python_error();}
  double value=operation==std::string("add")?a+b:operation==std::string("sub")?a-b:operation==std::string("mul")?a*b:a/b;
  return ac_call(cast,{[&]{return nb::float_(value);}});
 }
 if(kind=="i"||kind=="u"){
  nb::object ai=ac_check(PyNumber_Index(left.ptr())),bi=ac_check(PyNumber_Index(right.ptr()));
  uint64_t a=PyLong_AsUnsignedLongLongMask(ai.ptr()),b=PyLong_AsUnsignedLongLongMask(bi.ptr());if(PyErr_Occurred())throw nb::python_error();
  uint64_t result=operation==std::string("add")?a+b:operation==std::string("sub")?a-b:a*b;
  size_t bits=nb::cast<size_t>(ac_attr(dtype,"itemsize"))*8;if(bits<64)result&=(uint64_t(1)<<bits)-1;
  nb::object integer;if(kind=="i"&&(result&(uint64_t(1)<<(bits-1))))integer=ac_check(PyLong_FromLongLong(static_cast<int64_t>(result|(~uint64_t(0)<<(bits-1)))));else integer=ac_check(PyLong_FromUnsignedLongLong(result));
  return ac_call(cast,{[&]{return integer;}});
 }
 return ac_check(fn(left.ptr(),right.ptr()));
}
nb::object ac_binary(ACExpr left,ACExpr right,binaryfunc fn){nb::object a=left();nb::object b=right();if(ac_active_typing)return ac_typed_binary(std::move(a),std::move(b),fn);return ac_check(fn(a.ptr(),b.ptr()));}
nb::object ac_compare(ACExpr left,ACExpr right,int op){nb::object a=left();nb::object b=right();return ac_check(PyObject_RichCompare(a.ptr(),b.ptr(),op));}
// Chained comparison retains its one middle operand through the first truth test.
bool ac_chain_equal(ACExpr left,ACExpr middle,ACExpr last){
 nb::object a=left(),b=middle(),result;
 try{result=ac_check(PyObject_RichCompare(a.ptr(),b.ptr(),Py_EQ));}catch(...){a.reset();b.reset();throw;}
 a.reset();if(!ac_truth(std::move(result)))return false;
 nb::object c=last();try{result=ac_check(PyObject_RichCompare(b.ptr(),c.ptr(),Py_EQ));}catch(...){b.reset();c.reset();throw;}
 b.reset();c.reset();return ac_truth(std::move(result));
}
nb::object ac_subscript(ACExpr value,ACExpr key){nb::object a=value();nb::object k=key();return ac_item(std::move(a),std::move(k));}
void ac_set(ACExpr value,ACExpr target,ACExpr key){nb::object v=value();nb::object a=target();nb::object k=key();if(PyObject_SetItem(a.ptr(),k.ptr(),v.ptr())<0)throw nb::python_error();}
// AugAssign saves target/key before getitem/RHS and writes through those objects.
void ac_augmented_item(ACExpr target,ACExpr key,ACExpr right,binaryfunc operation){
 nb::object a=target(),k=key(),left,rhs,result;
 try{
  left=ac_check(PyObject_GetItem(a.ptr(),k.ptr()));rhs=right();
  try{result=ac_active_typing?ac_typed_binary(left,rhs,operation):ac_check(operation(left.ptr(),rhs.ptr()));}
  catch(...){left.reset();rhs.reset();throw;}
  left.reset();rhs.reset();
  if(PyObject_SetItem(a.ptr(),k.ptr(),result.ptr())<0){nb::python_error error;result.reset();a.reset();k.reset();throw error;}
  result.reset();a.reset();k.reset();
 }catch(...){left.reset();rhs.reset();result.reset();k.reset();a.reset();throw;}
}
nb::object ac_slice(nb::object start,nb::object stop,nb::object step=nb::none()){return ac_check(PySlice_New(start.ptr(),stop.ptr(),step.ptr()));}
std::vector<nb::object> ac_unpack(nb::object source,size_t n){
 PyObject* raw=PyObject_GetIter(source.ptr());if(!raw){if(PyErr_ExceptionMatches(PyExc_TypeError)&&Py_TYPE(source.ptr())->tp_iter==nullptr&&!PySequence_Check(source.ptr())){PyErr_Clear();PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(source.ptr())->tp_name);}throw nb::python_error();}
 nb::object iterator=nb::steal<nb::object>(raw);std::vector<nb::object> values;
 for(size_t i=0;i<n;++i){nb::object item=ac_next(iterator);if(!item.is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected %zu, got %zu)",n,values.size());throw nb::python_error();}values.push_back(std::move(item));}
 nb::object extra=ac_next(iterator);if(extra.is_valid()){PyErr_Format(PyExc_ValueError,"too many values to unpack (expected %zu)",n);throw nb::python_error();}iterator.reset();source.reset();return values;
}
struct AccelerationGlobals{
 nb::dict values;nb::object builtins;
 explicit AccelerationGlobals(nb::dict globals):values(std::move(globals)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())){}
 AccelerationGlobals(nb::dict globals,nb::object b):values(std::move(globals)),builtins(std::move(b)){}
 nb::object get(const char* name)const{nb::str key(name);PyObject* value=PyDict_GetItemWithError(values.ptr(),key.ptr());if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();value=PyObject_GetItem(builtins.ptr(),key.ptr());if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();nb::object error=ac_call(nb::borrow<nb::object>(PyExc_NameError),{[&]{return ac_check(PyUnicode_FromFormat("name '%s' is not defined",name));}});if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();}
 nb::object np(const char* name)const{return ac_attr(get("np"),name);}
};
struct ShapeAccelerationLocals{
 std::vector<std::string> names;std::vector<nb::object> refs;std::vector<bool> cells;
 ShapeAccelerationLocals()=default;ShapeAccelerationLocals(ShapeAccelerationLocals&&)=default;~ShapeAccelerationLocals(){clear();}
 size_t index(const char* name)const{auto it=std::find(names.begin(),names.end(),name);if(it==names.end())throw nb::value_error("unknown acceleration source local");return static_cast<size_t>(it-names.begin());}
 nb::object get(const char* name)const{size_t i=index(name);if(cells[i]){PyObject* value=PyCell_GET(refs[i].ptr());if(!value){PyErr_Format(PyExc_NameError,"cannot access free variable '%s' where it is not associated with a value in enclosing scope",name);throw nb::python_error();}return nb::borrow<nb::object>(value);}if(!refs[i].is_valid()){PyErr_Format(PyExc_UnboundLocalError,"cannot access local variable '%s' where it is not associated with a value",name);throw nb::python_error();}return refs[i];}
 nb::object view(const char* name)const{size_t i=index(name);PyObject* value=cells[i]?PyCell_GET(refs[i].ptr()):refs[i].ptr();return value?nb::borrow<nb::object>(value):nb::none();}
 void put(const char* name,nb::object value){size_t i=index(name);if(cells[i]){if(PyCell_Set(refs[i].ptr(),value.ptr())<0)throw nb::python_error();}else{nb::object old=std::move(refs[i]);refs[i]=std::move(value);old.reset();}}
 nb::object cell(const char* name)const{size_t i=index(name);if(!cells[i])throw nb::value_error("expected genuine acceleration closure cell");return refs[i];}
 void clear(){std::vector<nb::object> old;old.swap(refs);names.clear();cells.clear();for(auto& v:old)v.reset();}
};
ShapeAccelerationLocals shape_accel_locals(nb::tuple layout,nb::dict bindings,nb::dict getters){
 ShapeAccelerationLocals result;for(nb::handle value:layout){result.names.push_back(nb::cast<std::string>(value));result.refs.emplace_back();result.cells.push_back(false);}
 for(auto pair:bindings)result.put(nb::cast<std::string>(pair.first).c_str(),nb::borrow<nb::object>(pair.second));
 for(auto pair:getters){nb::handle fn=pair.second;if(!PyFunction_Check(fn.ptr()))throw nb::type_error("source-cell getter must be a genuine function");PyObject* closure=PyFunction_GetClosure(fn.ptr());if(!closure||PyTuple_GET_SIZE(closure)!=1)throw nb::value_error("source-cell getter must contain exactly one cell");size_t i=result.index(nb::cast<std::string>(pair.first).c_str());result.refs[i]=nb::borrow<nb::object>(PyTuple_GET_ITEM(closure,0));result.cells[i]=true;}
 return result;
}
int ac_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self))for(const auto& value:nb::inst_ptr<ShapeAccelerationLocals>(self)->refs)Py_VISIT(value.ptr());return 0;}
int ac_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ShapeAccelerationLocals>(self)->clear();return 0;}
PyType_Slot ac_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ac_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ac_owner_clear)},{0,nullptr}};
#define E(expr) [&]()->nb::object{return(expr);}
#define A(a,b) ac_binary(E(a),E(b),PyNumber_Add)
#define B(a,b) ac_binary(E(a),E(b),PyNumber_Subtract)
#define M(a,b) ac_binary(E(a),E(b),PyNumber_Multiply)
#define D(a,b) ac_binary(E(a),E(b),PyNumber_TrueDivide)
#define IA(a,b) ac_binary(E(a),E(b),PyNumber_InPlaceAdd)
#define IM(a,b) ac_binary(E(a),E(b),PyNumber_InPlaceMultiply)
#define ID(a,b) ac_binary(E(a),E(b),PyNumber_InPlaceTrueDivide)
#define C(a,b,op) ac_compare(E(a),E(b),op)
#define J(a,k) ac_subscript(E(a),E(k))
#define T(...) ac_tuple({__VA_ARGS__})
#define V(...) ac_list({__VA_ARGS__})
#define KW(key,value) ACKw{key,E(value)}
#define FN(name,...) ac_call(ns.get(name),{__VA_ARGS__})
#define NP(name,...) ac_call(ns.np(name),{__VA_ARGS__})
#define ARGS(...) std::initializer_list<ACExpr>{__VA_ARGS__}
#define NK(name,args,...) ac_call(ns.np(name),args,{__VA_ARGS__})
#define CALL(obj,name,...) ac_call(ac_attr((obj),name),{__VA_ARGS__})
#define O(name) o.get(#name)
#define P(name,value) o.put(#name,(value))
#define SET(target,key,value) ac_set(E(value),E(target),E(key))
#define ZERO nb::int_(0)
#define ONE nb::int_(1)
#define TWO nb::int_(2)
#define HALF (ac_active_typing?ac_call(ac_attr(ac_active_typing->numpy,"float64"),{[]{return nb::float_(0.5);}}):nb::float_(0.5))
#define ALL ac_slice(nb::none(),nb::none())
nb::object ac_neg(nb::object a){return ac_check(PyNumber_Negative(a.ptr()));}
nb::object ac_invert(nb::object a){return ac_check(PyNumber_Invert(a.ptr()));}
nb::object ac_power(nb::object a,nb::object b){return ac_check(PyNumber_Power(a.ptr(),b.ptr(),Py_None));}
nb::object ac_const(const nb::tuple& values,const char* text){for(nb::handle item:values)if(PyUnicode_Check(item.ptr())&&PyUnicode_CompareWithASCIIString(item.ptr(),text)==0)return nb::borrow<nb::object>(item);return nb::str(text);}
nb::object ac_literal(const nb::tuple& values,Py_ssize_t index){PyObject* value=PyTuple_GetItem(values.ptr(),index);if(!value)throw nb::python_error();return nb::borrow<nb::object>(value);}
[[noreturn]] void ac_raise(nb::object value){
 if(PyExceptionClass_Check(value.ptr())){
  nb::object klass=std::move(value);value=ac_call(klass);
  if(!PyExceptionInstance_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"calling %R should have returned an instance of BaseException, not %R",klass.ptr(),reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())));throw nb::python_error();}
 }
 if(PyExceptionInstance_Check(value.ptr()))PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr())),value.ptr());
 else PyErr_SetString(PyExc_TypeError,"exceptions must derive from BaseException");throw nb::python_error();
}
nb::object ac_len(AccelerationGlobals& ns,nb::object value){return FN("len",E(value));}
#define DEF(name) nb::object name(nb::dict globals,nb::tuple literals,ShapeAccelerationLocals& o)
#define BEGIN AccelerationGlobals ns(std::move(globals))

DEF(shape_accel_remove_duplicates_py){BEGIN;
 P(diff,NK("diff",ARGS(E(NK("append",ARGS(E(O(data)),E(J(O(data),T(E(ac_slice(ZERO,ONE)),E(ALL))))),KW("axis",ZERO)))),KW("axis",ZERO)));
 P(dup,NK("all",ARGS(E(C(O(diff),ZERO,Py_EQ))),KW("axis",ONE)));
 if(ac_truth(O(closed))&&ac_truth(J(O(dup),nb::int_(-1))))SET(O(dup),ZERO,nb::bool_(true));
 SET(O(dup),ac_slice(nb::int_(-2),nb::none()),nb::bool_(false));P(indices,NP("arange",E(J(ac_attr(O(data),"shape"),ZERO))));
 return J(O(data),J(O(indices),ac_invert(O(dup))));
}
DEF(shape_accel_mirror){BEGIN;return B(M(TWO,O(y)),O(x));}
DEF(shape_accel_sign_nonzero){BEGIN;P(y,CALL(NP("sign",E(O(x))),"astype",E(ac_attr(ns.get("np"),"int32"))));SET(O(y),C(O(y),ZERO,Py_EQ),ONE);return O(y);}
DEF(shape_accel_sign_cross){BEGIN;
 if(ac_chain_equal(E(J(ac_attr(O(x),"shape"),ONE)),E(J(ac_attr(O(y),"shape"),ONE)),E(TWO)))return FN("_sign_nonzero",E(B(M(J(O(x),T(E(ALL),E(ZERO))),J(O(y),T(E(ALL),E(ONE)))),M(J(O(x),T(E(ALL),E(ONE))),J(O(y),T(E(ALL),E(ZERO)))))));
 if(ac_chain_equal(E(J(ac_attr(O(x),"shape"),ONE)),E(J(ac_attr(O(y),"shape"),ONE)),E(nb::int_(3))))return FN("_sign_nonzero",E(NP("cross",E(O(x)),E(O(y)))));
 nb::object error=FN("ValueError",E(J(ac_attr(O(x),"shape"),ONE)),E(J(ac_attr(O(y),"shape"),ONE)));ac_raise(std::move(error));
}
DEF(shape_accel_cross2d){BEGIN;return B(M(J(O(a),ac_literal(literals,1)),J(O(b),ac_literal(literals,2))),M(J(O(a),ac_literal(literals,2)),J(O(b),ac_literal(literals,1))));}
DEF(shape_accel_orientation_py){BEGIN;P(val,B(M(B(J(O(q),ONE),J(O(p),ONE)),B(J(O(r),ZERO),J(O(q),ZERO))),M(B(J(O(q),ZERO),J(O(p),ZERO)),B(J(O(r),ONE),J(O(q),ONE)))));P(val,NP("sign",E(O(val))));return O(val);}
DEF(shape_accel_vectorized_orientation){BEGIN;P(val,FN("cross_2d",E(B(O(q),O(p))),E(B(O(r),O(p)))));return CALL(NP("sign",E(O(val))),"astype",E(ac_attr(ns.get("np"),"int8")));}
DEF(shape_accel_monotonic_py){BEGIN;
 if(ac_truth(C(J(ac_attr(O(poly),"shape"),ZERO),nb::int_(3),Py_LT)))return nb::bool_(false);
 if(ac_truth(C(O(orientation_),ONE,Py_EQ)))P(poly,J(O(poly),ac_slice(nb::none(),nb::none(),nb::int_(-1))));
 P(centroid,ac_call(ac_attr(O(poly),"mean"),{},{KW("axis",ZERO)}));
 P(angles,NP("arctan2",E(B(J(O(poly),T(E(ALL),E(ONE))),J(O(centroid),ONE))),E(B(J(O(poly),T(E(ALL),E(ZERO))),J(O(centroid),ZERO)))));
 P(shifted_angles,B(O(angles),J(O(angles),ZERO)));ac_augmented_item(E(O(shifted_angles)),E(C(O(shifted_angles),ZERO,Py_LT)),E(M(TWO,ac_attr(ns.get("np"),"pi"))),PyNumber_InPlaceAdd);
 return FN("bool",E(NP("all",E(C(NP("diff",E(O(shifted_angles))),ZERO,Py_GT)))));
}
DEF(shape_accel_common_orientation){BEGIN;
 if(ac_truth(C(J(ac_attr(O(poly),"shape"),ZERO),nb::int_(3),Py_LT)))return nb::none();
 P(fst,J(O(poly),ac_slice(nb::none(),nb::int_(-2))));P(snd,J(O(poly),ac_slice(ONE,nb::int_(-1))));P(thrd,J(O(poly),ac_slice(TWO,nb::none())));
 P(orn_set,NP("unique",E(FN("orientation",E(ac_attr(O(fst),"T")),E(ac_attr(O(snd),"T")),E(ac_attr(O(thrd),"T"))))));
 if(ac_truth(C(ac_attr(O(orn_set),"size"),ONE,Py_NE)))return nb::none();
 if(ac_truth(C(J(O(orn_set),ZERO),FN("orientation",E(J(O(poly),nb::int_(-2))),E(J(O(poly),nb::int_(-1))),E(J(O(poly),ZERO))),Py_EQ))&&ac_truth(C(J(O(orn_set),ZERO),FN("orientation",E(J(O(poly),nb::int_(-1))),E(J(O(poly),ZERO)),E(J(O(poly),ONE))),Py_EQ)))return FN("int",E(J(O(orn_set),ZERO)));
 return nb::none();
}
DEF(shape_accel_convex_py){BEGIN;P(orientation_,FN("_common_orientation",E(O(poly))));if(O(orientation_).is_none()||ac_truth(C(O(orientation_),ZERO,Py_EQ)))return nb::bool_(false);return FN("_are_polar_angles_monotonic",E(O(poly)),E(O(orientation_)));}
DEF(shape_accel_box_py){BEGIN;
 P(tl,J(O(bounding_box),ac_literal(literals,1)));
 P(br,J(O(bounding_box),ac_literal(literals,2)));
 P(tr,J(O(bounding_box),ac_literal(literals,3)));
 P(bl,J(O(bounding_box),ac_literal(literals,4)));
 return NP("array",E(V(E(O(tl)),E(D(A(O(tl),O(tr)),TWO)),E(O(tr)),E(D(A(O(tr),O(br)),TWO)),E(O(br)),E(D(A(O(br),O(bl)),TWO)),E(O(bl)),E(D(A(O(bl),O(tl)),TWO)),E(D(A(O(tl),O(br)),TWO)))));
}
// CPython inlined set comprehension: one current source edge survives next(),
// with iterator/partial-set unwind before restoration of the previous binding.
nb::object ac_edge_set(ShapeAccelerationLocals& o,nb::object iterable,bool reverse,bool publish){
 nb::object iterator=ac_iter(std::move(iterable));size_t index=o.index("edge");nb::object saved=std::move(o.refs[index]),partial;
 try{
  partial=ac_check(PySet_New(nullptr));
  while(nb::object value=ac_next(iterator)){
   o.put("edge",std::move(value));nb::object edge=o.get("edge");
   nb::tuple pair=reverse?ac_tuple({[&]{return ac_item(edge,nb::int_(1));},[&]{return ac_item(edge,nb::int_(0));}}):ac_tuple({[&]{return ac_item(edge,nb::int_(0));},[&]{return ac_item(edge,nb::int_(1));}});
   edge.reset();if(PySet_Add(partial.ptr(),pair.ptr())<0)throw nb::python_error();
  }
  iterator.reset();if(publish)o.put("unvisited_edges",std::move(partial));o.put("edge",std::move(saved));return partial;
 }catch(...){iterator.reset();partial.reset();o.put("edge",std::move(saved));throw;}
}
DEF(shape_accel_reconstruct_py){BEGIN;
 P(adjacency,FN("defaultdict",E(ns.get("list"))));nb::object iterator=ac_iter(O(edges));
 while(nb::object value=ac_next(iterator)){P(edge_,std::move(value));auto pair=ac_unpack(O(edge_),2);P(v1,std::move(pair[0]));P(v2,std::move(pair[1]));CALL(J(O(adjacency),O(v1)),"append",E(O(v2)));CALL(J(O(adjacency),O(v2)),"append",E(O(v1)));}iterator.reset();
 ac_edge_set(o,O(edges),false,true);
 nb::object update=ac_attr(O(unvisited_edges),"update");nb::object reverse=ac_edge_set(o,O(edges),true,false);ac_call(std::move(update),{E(reverse)});reverse.reset();
 P(polygons,nb::list());while(ac_truth(O(unvisited_edges))){P(edge,FN("next",E(FN("iter",E(O(unvisited_edges))))));P(current_vertex,J(O(edge),ZERO));P(start_vertex,J(O(edge),ONE));P(polygon_indices,V(E(O(start_vertex))));
  CALL(O(unvisited_edges),"discard",E(T(E(O(start_vertex)),E(O(current_vertex)))));CALL(O(unvisited_edges),"discard",E(T(E(O(current_vertex)),E(O(start_vertex)))));
  while(ac_truth(C(O(current_vertex),J(O(polygon_indices),ZERO),Py_NE))){CALL(O(polygon_indices),"append",E(O(current_vertex)));P(next_vertex,nb::none());nb::object neighbors=ac_iter(J(O(adjacency),O(current_vertex)));
   while(nb::object value=ac_next(neighbors)){P(neighbor,std::move(value));nb::tuple key=T(E(O(current_vertex)),E(O(neighbor)));int present=PySequence_Contains(O(unvisited_edges).ptr(),key.ptr());if(present<0)throw nb::python_error();if(present){P(next_vertex,O(neighbor));CALL(O(unvisited_edges),"discard",E(T(E(O(current_vertex)),E(O(next_vertex)))));CALL(O(unvisited_edges),"discard",E(T(E(O(next_vertex)),E(O(current_vertex)))));break;}}
   neighbors.reset();if(O(next_vertex).is_none())break;P(current_vertex,O(next_vertex));
  }P(polygon_vertices,J(O(vertices),O(polygon_indices)));CALL(O(polygons),"append",E(O(polygon_vertices)));
 }return O(polygons);
}
DEF(shape_accel_normalize_py){BEGIN;
 if(ac_truth(C(FN("tuple",E(J(O(vertices),ZERO))),FN("tuple",E(J(O(vertices),nb::int_(-1)))),Py_EQ))){P(vertices,J(O(vertices),ac_slice(nb::none(),nb::int_(-1))));P(close,nb::bool_(true));}
 P(vertex_to_idx,nb::dict());P(new_vertices,nb::list());P(edges,FN("set"));P(prev_idx,ZERO);P(i,ZERO);nb::object iterator=ac_iter(O(vertices));
 while(nb::object value=ac_next(iterator)){P(vertex,std::move(value));P(vertex_t,T(E(J(O(vertex),ZERO)),E(J(O(vertex),ONE))));P(current_idx,CALL(O(vertex_to_idx),"setdefault",E(O(vertex_t)),E(O(i))));
  if(ac_truth(C(O(current_idx),O(i),Py_EQ))){CALL(O(new_vertices),"append",E(O(vertex)));P(i,IA(O(i),ONE));}
  P(edge,ac_truth(C(O(prev_idx),O(current_idx),Py_LT))?T(E(O(prev_idx)),E(O(current_idx))):T(E(O(current_idx)),E(O(prev_idx))));
  int exists=PySequence_Contains(O(edges).ptr(),O(edge).ptr());if(exists<0)throw nb::python_error();CALL(O(edges),exists?"remove":"add",E(O(edge)));P(prev_idx,O(current_idx));
 }iterator.reset();if(ac_truth(O(close))){P(vertex_t,FN("tuple",E(J(O(vertices),nb::int_(-1)))));P(idx,J(O(vertex_to_idx),O(vertex_t)));P(edge,T(E(ZERO),E(O(idx))));int exists=PySequence_Contains(O(edges).ptr(),O(edge).ptr());if(exists<0)throw nb::python_error();CALL(O(edges),exists?"remove":"add",E(O(edge)));}
 CALL(O(edges),"remove",E(ac_literal(literals,6)));P(new_vertices_array,NK("array",ARGS(E(O(new_vertices))),KW("dtype",ac_attr(ns.get("np"),"float32"))));P(edges_array,NK("array",ARGS(E(FN("list",E(O(edges))))),KW("dtype",ac_attr(ns.get("np"),"int64"))));return T(E(O(new_vertices_array)),E(O(edges_array)));
}
DEF(shape_accel_segment_normal){BEGIN;
 P(d,B(O(b),O(a)));if(ac_truth(C(ac_attr(O(d),"ndim"),ONE,Py_EQ))){
  if(ac_truth(C(ac_len(ns,O(d)),TWO,Py_EQ)))P(normal,NP("array",E(V(E(J(O(d),ONE)),E(ac_neg(J(O(d),ZERO)))))));
  else P(normal,NP("cross",E(O(d)),E(O(p))));
  P(norm,ac_call(ac_attr(ac_attr(ns.get("np"),"linalg"),"norm"),{E(O(normal))}));if(ac_truth(C(O(norm),ZERO,Py_EQ)))P(norm,ONE);
 }else{
  P(normal,ac_truth(C(J(ac_attr(O(d),"shape"),ONE),TWO,Py_EQ))?CALL(NK("stack",ARGS(E(V(E(J(O(d),T(E(ALL),E(ONE)))),E(ac_neg(J(O(d),T(E(ALL),E(ZERO)))))))),KW("axis",ZERO)),"transpose",E(ONE),E(ZERO)):NP("cross",E(O(d)),E(O(p))));
  P(norm,ac_call(ac_attr(ac_attr(ns.get("np"),"linalg"),"norm"),{E(O(normal))},{KW("axis",ONE),KW("keepdims",nb::bool_(true))}));P(ind,C(O(norm),ZERO,Py_EQ));SET(O(norm),O(ind),ONE);
 }return D(O(normal),O(norm));
}

struct ShapeAccelerationIterator{
 nb::object iterator,centers,offsets,triangles,namespace_,builtins,current;bool started=false,closed=false,running=false;
 ShapeAccelerationIterator(nb::object it,nb::object c,nb::object o,nb::object t,AccelerationGlobals& ns):iterator(std::move(it)),centers(std::move(c)),offsets(std::move(o)),triangles(std::move(t)),namespace_(ns.values),builtins(ns.builtins){}
 ShapeAccelerationIterator(ShapeAccelerationIterator&&)=default;
 void clear(){nb::object a=std::move(iterator),b=std::move(current),c=std::move(centers),d=std::move(offsets),e=std::move(triangles),f=std::move(namespace_),g=std::move(builtins);a.reset();b.reset();c.reset();d.reset();e.reset();f.reset();g.reset();}
 ~ShapeAccelerationIterator(){clear();}
 nb::object read(nb::handle cell,const char* name){PyObject* v=PyCell_GET(cell.ptr());if(!v){PyErr_Format(PyExc_NameError,"cannot access free variable '%s' where it is not associated with a value in enclosing scope",name);throw nb::python_error();}return nb::borrow<nb::object>(v);}
 nb::object send(nb::object sent){if(running)throw nb::value_error("generator already executing");if(closed)throw nb::stop_iteration();if(!started&&!sent.is_none())throw nb::type_error("can't send non-None value to a just-started generator");running=true;started=true;
  try{nb::object next=ac_next(iterator);if(!next.is_valid()){closed=true;running=false;clear();throw nb::stop_iteration();}nb::object old=std::move(current);current=std::move(next);old.reset();nb::object result=J(J(A(read(centers,"centers"),read(offsets,"offsets")),read(triangles,"triangles")),T(E(ALL),E(current)));running=false;return result;}
  catch(const nb::python_error& error){running=false;closed=true;if(error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}catch(...){running=false;throw;}
 }
 void close(){if(running)throw nb::value_error("generator already executing");if(closed)return;closed=true;clear();}
 nb::object throw_(nb::args args){if(running)throw nb::value_error("generator already executing");nb::object error=collections::normalize_throw(args);bool was_closed=closed;closed=true;collections::raise_exception(error,!was_closed);}
};
int ac_iterator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& s=*nb::inst_ptr<ShapeAccelerationIterator>(self);Py_VISIT(s.iterator.ptr());Py_VISIT(s.centers.ptr());Py_VISIT(s.offsets.ptr());Py_VISIT(s.triangles.ptr());Py_VISIT(s.namespace_.ptr());Py_VISIT(s.builtins.ptr());Py_VISIT(s.current.ptr());}return 0;}
int ac_iterator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ShapeAccelerationIterator>(self)->clear();return 0;}
PyType_Slot ac_iterator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(ac_iterator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(ac_iterator_clear)},{0,nullptr}};
nb::object shape_accel_edges_py(nb::dict globals,nb::tuple literals,ShapeAccelerationLocals& o,nb::object generator_factory){BEGIN;
 P(path,NK("asarray",ARGS(E(O(path))),KW("dtype",ns.get("float"))));if(ac_truth(O(closed)))P(path,NP("concatenate",E(T(E(O(path)),E(V(E(J(O(path),ZERO))))))));
 if(ac_truth(O(closed))){P(_ext_point1,J(O(path),nb::int_(-2)));P(_ext_point2,J(O(path),ONE));}
 else{P(_ext_point1,FN("_mirror_point",E(J(O(path),ONE)),E(J(O(path),ZERO))));P(_ext_point2,FN("_mirror_point",E(J(O(path),nb::int_(-2))),E(J(O(path),nb::int_(-1)))));}
 P(full_path,NK("concatenate",ARGS(E(T(E(V(E(O(_ext_point1)))),E(O(path)),E(V(E(O(_ext_point2))))) )),KW("axis",ZERO)));
 P(full_normals,FN("segment_normal",E(J(O(full_path),ac_slice(nb::none(),nb::int_(-1)))),E(J(O(full_path),ac_slice(ONE,nb::none())))));
 P(miters,M(HALF,A(J(O(full_normals),ac_slice(nb::none(),nb::int_(-1))),J(O(full_normals),ac_slice(ONE,nb::none())))));
 P(mf_dot,NP("expand_dims",E(NP("einsum",E(ac_const(literals,"ij,ij->i")),E(O(miters)),E(J(O(full_normals),ac_slice(nb::none(),nb::int_(-1)))))),E(nb::int_(-1))));
 P(miters,NK("divide",ARGS(E(O(miters)),E(O(mf_dot))),KW("out",NP("zeros_like",E(O(miters)))),KW("where",C(NP("abs",E(O(mf_dot))),nb::float_(1e-10),Py_GT))));
 P(miter_lengths_squared,ac_call(ac_attr(ac_power(O(miters),TWO),"sum"),{},{KW("axis",ONE)}));
 P(miter_signs,FN("_sign_cross",E(J(O(full_normals),ac_slice(ONE,nb::none()))),E(J(O(full_normals),ac_slice(nb::none(),nb::int_(-1))))));P(miters,M(HALF,O(miters)));
 P(centers,NK("repeat",ARGS(E(O(path)),E(TWO)),KW("axis",ZERO)));P(offsets,NK("repeat",ARGS(E(O(miters)),E(TWO)),KW("axis",ZERO)));ac_augmented_item(E(O(offsets)),E(ac_slice(nb::none(),nb::none(),TWO)),E(nb::int_(-1)),PyNumber_InPlaceMultiply);
 P(triangles0,NP("tile",E(NP("array",E(V(E(V(E(ZERO),E(ONE),E(nb::int_(3)))),E(V(E(ZERO),E(nb::int_(3)),E(TWO))))))),E(T(E(B(ac_len(ns,O(path)),ONE)),E(ONE)))));
 P(triangles,A(O(triangles0),M(TWO,NP("repeat",E(J(NP("arange",E(B(ac_len(ns,O(path)),ONE))),T(E(ALL),E(ac_attr(ns.get("np"),"newaxis"))))),E(TWO),E(ZERO)))));
 P(idx_bevel,J(NP("where",E(NP("bitwise_or",E(O(bevel)),E(C(O(miter_lengths_squared),ac_power(O(limit),TWO),Py_GT))))),ZERO));
 if(ac_truth(C(ac_len(ns,O(idx_bevel)),ZERO,Py_GT))){
  P(idx_offset,CALL(C(J(O(miter_signs),O(idx_bevel)),ZERO,Py_LT),"astype",E(ns.get("int"))));P(idx_bevel_outside,A(M(TWO,O(idx_bevel)),O(idx_offset)));P(idx_bevel_inside,A(M(TWO,O(idx_bevel)),B(ONE,O(idx_offset))));P(sign_bevel,NP("expand_dims",E(J(O(miter_signs),O(idx_bevel))),E(nb::int_(-1))));
  SET(O(offsets),O(idx_bevel_outside),M(M(nb::float_(-0.5),J(J(O(full_normals),ac_slice(nb::none(),nb::int_(-1))),O(idx_bevel))),O(sign_bevel)));
  ac_augmented_item(E(O(offsets)),E(O(idx_bevel_inside)),E(NP("sqrt",E(J(O(miter_lengths_squared),T(E(O(idx_bevel)),E(ac_attr(ns.get("np"),"newaxis"))))))),PyNumber_InPlaceTrueDivide);
  P(_nonspecial,C(O(idx_bevel),B(ac_len(ns,O(path)),ONE),Py_NE));P(idx_bevel,J(O(idx_bevel),O(_nonspecial)));P(idx_bevel_outside,J(O(idx_bevel_outside),O(_nonspecial)));P(sign_bevel,J(O(sign_bevel),O(_nonspecial)));P(idx_offset,J(O(idx_offset),O(_nonspecial)));
  P(centers_bevel,J(O(path),O(idx_bevel)));P(offsets_bevel,M(M(nb::float_(-0.5),J(J(O(full_normals),ac_slice(ONE,nb::none())),O(idx_bevel))),O(sign_bevel)));P(n_centers,ac_len(ns,O(centers)));
  SET(O(triangles),T(E(M(TWO,O(idx_bevel))),E(O(idx_offset))),A(ac_len(ns,O(centers)),NP("arange",E(ac_len(ns,O(idx_bevel))))));
  SET(O(triangles),T(E(A(M(TWO,O(idx_bevel)),B(ONE,O(idx_offset)))),E(O(idx_offset))),A(O(n_centers),NP("arange",E(ac_len(ns,O(idx_bevel))))));
  P(triangles0,NP("tile",E(NP("array",E(V(E(V(E(ZERO),E(ONE),E(TWO))))))),E(T(E(ac_len(ns,O(idx_bevel))),E(ONE)))));
  P(triangles_bevel,ac_attr(NP("array",E(V(E(A(M(TWO,O(idx_bevel)),O(idx_offset))),E(A(M(TWO,O(idx_bevel)),B(ONE,O(idx_offset)))),E(A(O(n_centers),NP("arange",E(ac_len(ns,O(idx_bevel))))))))),"T"));
  P(centers,NP("concatenate",E(V(E(O(centers)),E(O(centers_bevel))))));P(offsets,NP("concatenate",E(V(E(O(offsets)),E(O(offsets_bevel))))));P(triangles,NP("concatenate",E(V(E(O(triangles)),E(O(triangles_bevel))))));
 }
 nb::object tuple_fn=ns.get("tuple");nb::object range=FN("range",E(nb::int_(3)));nb::object iterator=ac_iter(std::move(range));nb::object native=nb::cast(ShapeAccelerationIterator(std::move(iterator),o.cell("centers"),o.cell("offsets"),o.cell("triangles"),ns));nb::object gen=ac_call(std::move(generator_factory),{E(native)});
 if(PyObject_SetAttrString(gen.ptr(),"__name__",nb::str("<genexpr>").ptr())<0||PyObject_SetAttrString(gen.ptr(),"__qualname__",nb::str("generate_2D_edge_meshes_py.<locals>.<genexpr>").ptr())<0)throw nb::python_error();
 auto abc=ac_unpack(ac_call(std::move(tuple_fn),{E(gen)}),3);P(a,std::move(abc[0]));P(b,std::move(abc[1]));P(c,std::move(abc[2]));gen.reset();native.reset();
 P(flip_idx,C(FN("_sign_cross",E(B(O(b),O(a))),E(B(O(c),O(a)))),ZERO,Py_LT));SET(O(triangles),O(flip_idx),NK("flip",ARGS(E(J(O(triangles),O(flip_idx)))),KW("axis",nb::int_(-1))));
 return T(E(O(centers)),E(O(offsets)),E(O(triangles)));
}

DEF(shape_accel_numba_dot){BEGIN;return A(M(J(O(v0),ZERO),J(O(v1),ZERO)),M(J(O(v0),ONE),J(O(v1),ONE)));}
DEF(shape_accel_numba_cross){BEGIN;return B(M(J(O(v0),ZERO),J(O(v1),ONE)),M(J(O(v0),ONE),J(O(v1),ZERO)));}
DEF(shape_accel_numba_sign_abs){BEGIN;if(ac_truth(C(O(f),ZERO,Py_GT)))return T(E(nb::float_(1.0)),E(O(f)));if(ac_truth(C(O(f),ZERO,Py_LT)))return T(E(nb::float_(-1.0)),E(ac_neg(O(f))));return ac_literal(literals,4);}
DEF(shape_accel_numba_orthogonal){BEGIN;
 if(ac_truth(O(ccw)))return NP("array",E(V(E(J(O(v),ONE)),E(ac_neg(J(O(v),ZERO))))));
 return NP("array",E(V(E(ac_neg(J(O(v),ONE))),E(J(O(v),ZERO)))));
}
DEF(shape_accel_numba_output_size){BEGIN;
 P(n,ac_len(ns,O(direction_vectors)));P(point_count,A(M(TWO,O(n)),M(TWO,O(closed))));if(ac_truth(O(bevel))){P(bevel_count,B(O(n),M(TWO,nb::bool_(!ac_truth(O(closed))))));return A(O(point_count),O(bevel_count));}
 P(bevel_count,ZERO);P(start,B(ONE,O(closed)));P(stop,A(B(O(n),ONE),O(closed)));nb::object iterator=ac_iter(FN("range",E(O(start)),E(O(stop))));
 while(nb::object value=ac_next(iterator)){P(i,std::move(value));P(cos_angle,FN("_dot",E(J(O(direction_vectors),B(O(i),ONE))),E(J(O(direction_vectors),O(i)))));if(ac_truth(C(O(cos_angle),O(cos_miter_limit),Py_LT)))P(bevel_count,IA(O(bevel_count),ONE));}iterator.reset();return A(O(point_count),O(bevel_count));
}
DEF(shape_accel_numba_set_centers){BEGIN;
 P(cos_angle,FN("_dot",E(O(vec1)),E(O(vec2))));P(sin_angle,FN("_cross_z",E(O(vec1)),E(O(vec2))));P(bevel,ac_truth(O(always_bevel))?O(always_bevel):C(O(cos_angle),O(cos_limit),Py_LT));
 nb::object iterator=ac_iter(FN("range",E(A(TWO,O(bevel)))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));SET(O(centers),A(O(j),O(i)),O(vertex));}iterator.reset();
 if(ac_truth(C(O(sin_angle),ZERO,Py_EQ)))P(miter,M(NK("array",ARGS(E(V(E(J(O(vec1),ONE)),E(ac_neg(J(O(vec1),ZERO)))))),KW("dtype",ac_attr(ns.get("np"),"float32"))),HALF));
 else{P(scale_factor,D(ONE,O(sin_angle)));if(ac_truth(O(bevel))){auto pair=ac_unpack(FN("_sign_abs",E(O(scale_factor))),2);P(sign,std::move(pair[0]));P(mag,std::move(pair[1]));P(scale_factor,M(O(sign),FN("min",E(O(mag)),E(O(half_vec1_len)),E(O(half_vec2_len)))));}P(miter,M(M(B(O(vec1),O(vec2)),HALF),O(scale_factor)));}
 if(ac_truth(O(bevel))){P(clockwise,C(O(sin_angle),ZERO,Py_LT));P(counterclockwise,nb::bool_(!ac_truth(O(clockwise))));P(invert,ac_truth(O(counterclockwise))?nb::float_(-1.0):nb::float_(1.0));
  SET(O(offsets),A(O(j),O(counterclockwise)),M(O(invert),O(miter)));SET(O(offsets),A(O(j),O(clockwise)),M(HALF,ac_call(ns.get("_orthogonal_vector"),{E(O(vec1))},{KW("ccw",O(counterclockwise))})));SET(O(offsets),A(O(j),TWO),M(HALF,ac_call(ns.get("_orthogonal_vector"),{E(O(vec2))},{KW("ccw",O(counterclockwise))})));
  SET(O(triangles),O(j),V(E(O(j)),E(A(O(j),ONE)),E(A(O(j),TWO))));SET(O(triangles),A(O(j),ONE),V(E(A(O(j),O(counterclockwise))),E(A(O(j),TWO)),E(A(O(j),nb::int_(3)))));SET(O(triangles),A(O(j),TWO),V(E(A(A(O(j),ONE),O(clockwise))),E(A(O(j),nb::int_(3))),E(A(O(j),nb::int_(4)))));return nb::int_(3);
 }
 SET(O(offsets),O(j),O(miter));SET(O(offsets),A(O(j),ONE),ac_neg(O(miter)));SET(O(triangles),O(j),V(E(O(j)),E(A(O(j),ONE)),E(A(O(j),TWO))));SET(O(triangles),A(O(j),ONE),V(E(A(O(j),ONE)),E(A(O(j),TWO)),E(A(O(j),nb::int_(3)))));return TWO;
}
DEF(shape_accel_numba_orientation){BEGIN;P(val,B(M(B(J(O(p2),ZERO),J(O(p1),ZERO)),B(J(O(p3),ONE),J(O(p1),ONE))),M(B(J(O(p2),ONE),J(O(p1),ONE)),B(J(O(p3),ZERO),J(O(p1),ZERO)))));if(ac_truth(C(O(val),ZERO,Py_LT)))return ac_attr(ns.get("Orientation"),"clockwise");if(ac_truth(C(O(val),ZERO,Py_GT)))return ac_attr(ns.get("Orientation"),"anticlockwise");return ac_attr(ns.get("Orientation"),"collinear");}
DEF(shape_accel_numba_normalize_triangles){BEGIN;
 nb::object iterator=ac_iter(FN("range",E(ac_len(ns,O(triangles)))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));P(ti,J(O(triangles),O(i)));P(p1,A(J(O(centers),J(O(ti),ZERO)),J(O(offsets),J(O(ti),ZERO))));P(p2,A(J(O(centers),J(O(ti),ONE)),J(O(offsets),J(O(ti),ONE))));P(p3,A(J(O(centers),J(O(ti),TWO)),J(O(offsets),J(O(ti),TWO))));if(ac_truth(C(FN("_orientation",E(O(p1)),E(O(p2)),E(O(p3))),ac_attr(ns.get("Orientation"),"clockwise"),Py_EQ)))SET(O(triangles),O(i),V(E(J(O(ti),TWO)),E(J(O(ti),ONE)),E(J(O(ti),ZERO))));}iterator.reset();return nb::none();
}
// Only compiled polar calls use the genuine math atan2 scalar signature.
// Python/py_func calls retain their original Python math callable and results.
nb::object ac_polar_angles_compiled_atan2(AccelerationGlobals& ns,std::initializer_list<ACExpr> arguments){
 nb::object function=ns.get("atan2");if(!ac_active_typing||!PyCFunction_Check(function.ptr())||nb::cast<std::string>(ac_attr(function,"__name__"))!="atan2"||nb::cast<std::string>(ac_attr(function,"__module__"))!="math")return ac_call(function,arguments);
 if(arguments.size()!=2)throw nb::type_error("compiled polar atan2 requires two arguments");auto argument=arguments.begin();nb::object y=(*argument++)();nb::object x=(*argument)();auto& metadata=*ac_active_typing;
 nb::object y_type=ac_call(ac_attr(metadata.context,"resolve_value_type"),{E(y)}),x_type=ac_call(ac_attr(metadata.context,"resolve_value_type"),{E(x)}),function_type=ac_call(ac_attr(metadata.context,"resolve_value_type"),{E(function)}),signature=ac_call(ac_attr(metadata.context,"resolve_function_type"),{E(function_type),E(T(E(y_type),E(x_type))),E(nb::dict())});if(signature.is_none())throw nb::type_error("Numba cannot type compiled polar atan2");
 nb::object result_type=ac_attr(signature,"return_type"),dtype=ac_call(metadata.as_dtype,{E(result_type)});std::string kind=nb::cast<std::string>(ac_attr(dtype,"kind"));const int bytes=nb::cast<int>(ac_attr(dtype,"itemsize"));if(kind!="f"||(bytes!=4&&bytes!=8))return ac_call(function,{E(y),E(x)});
 double left=PyFloat_AsDouble(y.ptr());if(PyErr_Occurred())throw nb::python_error();double right=PyFloat_AsDouble(x.ptr());if(PyErr_Occurred())throw nb::python_error();double result;if(bytes==4){volatile float value=std::atan2(static_cast<float>(left),static_cast<float>(right));result=static_cast<double>(value);}else{volatile double value=std::atan2(left,right);result=value;}
 return ac_call(ac_attr(dtype,"type"),{E(nb::float_(result))});
}
DEF(shape_accel_numba_monotonic){BEGIN;
 P(start_angle,ac_polar_angles_compiled_atan2(ns,{E(B(J(J(O(vertices),ZERO),ONE),J(O(centroid),ONE))),E(B(J(J(O(vertices),ZERO),ZERO),J(O(centroid),ZERO)))}));P(prev_angle,nb::float_(0.0));
 nb::object iterator=ac_iter(FN("range",E(ONE),E(ac_len(ns,O(vertices)))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));P(angle,B(ac_polar_angles_compiled_atan2(ns,{E(B(J(J(O(vertices),O(i)),ONE),J(O(centroid),ONE))),E(B(J(J(O(vertices),O(i)),ZERO),J(O(centroid),ZERO)))}),O(start_angle)));if(ac_truth(C(O(angle),ZERO,Py_LT)))P(angle,IA(O(angle),M(TWO,ns.get("pi"))));if(ac_truth(C(O(angle),O(prev_angle),Py_LT)))return nb::bool_(false);P(prev_angle,O(angle));}iterator.reset();return nb::bool_(true);
}
DEF(shape_accel_numba_convex){BEGIN;
 if(ac_truth(C(ac_len(ns,O(vertices)),nb::int_(3),Py_LT)))return nb::bool_(false);if(ac_truth(C(ac_len(ns,O(vertices)),nb::int_(3),Py_EQ)))return nb::bool_(true);
 P(orientation_,ac_attr(ns.get("Orientation"),"collinear"));P(idx,ZERO);P(n_points,J(ac_attr(O(vertices),"shape"),ZERO));P(orientation_set,nb::bool_(false));nb::object iterator=ac_iter(FN("range",E(B(O(n_points),TWO))));
 while(nb::object value=ac_next(iterator)){P(idx,std::move(value));P(p1,J(O(vertices),O(idx)));P(p2,J(O(vertices),A(O(idx),ONE)));P(p3,J(O(vertices),A(O(idx),TWO)));P(current_orientation,FN("_orientation",E(O(p1)),E(O(p2)),E(O(p3))));if(ac_truth(C(O(current_orientation),ac_attr(ns.get("Orientation"),"collinear"),Py_NE))){if(!ac_truth(O(orientation_set))){P(orientation_,O(current_orientation));P(orientation_set,nb::bool_(true));}else if(ac_truth(C(O(current_orientation),O(orientation_),Py_NE)))return nb::bool_(false);}}iterator.reset();
 if(ac_truth(C(O(orientation_),ac_attr(ns.get("Orientation"),"collinear"),Py_EQ)))return nb::bool_(false);
 iterator=ac_iter(V(E(T(E(B(O(n_points),TWO)),E(B(O(n_points),ONE)),E(ZERO))),E(T(E(B(O(n_points),ONE)),E(ZERO),E(ONE)))));
 while(nb::object value=ac_next(iterator)){auto triple=ac_unpack(std::move(value),3);P(idx0,std::move(triple[0]));P(idx1,std::move(triple[1]));P(idx2,std::move(triple[2]));P(triangle_orientation,FN("_orientation",E(J(O(vertices),O(idx0))),E(J(O(vertices),O(idx1))),E(J(O(vertices),O(idx2)))));nb::list accepted=V(E(ac_attr(ns.get("Orientation"),"collinear")),E(O(orientation_)));int contains=PySequence_Contains(accepted.ptr(),O(triangle_orientation).ptr());if(contains<0)throw nb::python_error();if(!contains)return nb::bool_(false);}iterator.reset();
 P(centroid,NK("empty",ARGS(E(TWO)),KW("dtype",ac_attr(ns.get("np"),"float32"))));SET(O(centroid),ZERO,NP("mean",E(J(O(vertices),T(E(ALL),E(ZERO))))));SET(O(centroid),ONE,NP("mean",E(J(O(vertices),T(E(ALL),E(ONE))))));
 if(ac_truth(C(O(orientation_),ac_attr(ns.get("Orientation"),"anticlockwise"),Py_EQ)))return FN("_are_polar_angles_monotonic",E(O(vertices)),E(O(centroid)));return FN("_are_polar_angles_monotonic",E(J(O(vertices),ac_slice(nb::none(),nb::none(),nb::int_(-1)))),E(O(centroid)));
}
DEF(shape_accel_numba_directions){BEGIN;
 P(normals,NP("empty_like",E(O(path))));P(vec_len_arr,NK("empty",ARGS(E(ac_len(ns,O(path)))),KW("dtype",ac_attr(ns.get("np"),"float32"))));
 nb::object iterator=ac_iter(FN("range",E(B(ONE,O(closed))),E(ac_len(ns,O(path)))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));P(vec_diff,B(J(O(path),O(i)),J(O(path),B(O(i),ONE))));SET(O(vec_len_arr),B(O(i),ONE),NP("sqrt",E(A(ac_power(J(O(vec_diff),ZERO),TWO),ac_power(J(O(vec_diff),ONE),TWO)))));SET(O(normals),B(O(i),ONE),D(O(vec_diff),J(O(vec_len_arr),B(O(i),ONE))));}iterator.reset();return T(E(O(normals)),E(M(O(vec_len_arr),HALF)));
}
DEF(shape_accel_numba_edges_loop){BEGIN;
 P(j,ZERO);if(!ac_truth(O(closed))){SET(O(centers),ac_slice(nb::none(),TWO),J(O(path),ZERO));SET(O(offsets),ZERO,M(HALF,ac_call(ns.get("_orthogonal_vector"),{E(J(O(direction_vectors),ZERO))},{KW("ccw",nb::bool_(true))})));SET(O(offsets),ONE,ac_neg(J(O(offsets),ZERO)));SET(O(triangles),ZERO,V(E(ZERO),E(ONE),E(TWO)));SET(O(triangles),ONE,V(E(ONE),E(TWO),E(nb::int_(3))));P(j,TWO);}
 nb::object iterator=ac_iter(FN("range",E(B(ONE,O(closed))),E(A(B(ac_len(ns,O(direction_vectors)),ONE),O(closed)))));
 while(nb::object value=ac_next(iterator)){P(i,std::move(value));P(j,IA(O(j),FN("_set_centers_and_offsets",E(O(centers)),E(O(offsets)),E(O(triangles)),E(J(O(path),O(i))),E(J(O(direction_vectors),B(O(i),ONE))),E(J(O(direction_vectors),O(i))),E(J(O(bevel_limit_array),B(O(i),ONE))),E(J(O(bevel_limit_array),O(i))),E(O(j)),E(O(cos_limit)),E(O(bevel)))));}iterator.reset();
 if(ac_truth(O(closed))){SET(O(centers),O(j),J(O(centers),ZERO));SET(O(centers),A(O(j),ONE),J(O(centers),ONE));SET(O(offsets),O(j),J(O(offsets),ZERO));SET(O(offsets),A(O(j),ONE),J(O(offsets),ONE));}
 else{SET(O(centers),O(j),J(O(path),nb::int_(-1)));SET(O(centers),A(O(j),ONE),J(O(path),nb::int_(-1)));SET(O(offsets),O(j),M(HALF,FN("_orthogonal_vector",E(J(O(direction_vectors),nb::int_(-2))))));SET(O(offsets),A(O(j),ONE),ac_neg(J(O(offsets),O(j))));}return nb::none();
}
DEF(shape_accel_numba_cut_end){BEGIN;P(path_,NK("asarray",ARGS(E(O(path))),KW("dtype",ac_attr(ns.get("np"),"float32"))));if(ac_truth(NP("all",E(C(J(O(path_),nb::int_(-1)),J(O(path_),nb::int_(-2)),Py_EQ)))))return J(O(path_),ac_slice(nb::none(),nb::int_(-1)));return O(path_);}
DEF(shape_accel_numba_edges){BEGIN;
 P(path,FN("_cut_end_if_repetition",E(O(path))));if(ac_truth(C(ac_len(ns,O(path)),TWO,Py_LT))){P(centers,NK("empty",ARGS(E(ac_literal(literals,2))),KW("dtype",ac_attr(ns.get("np"),"float32"))));SET(O(centers),ZERO,J(O(path),ZERO));SET(O(centers),ONE,J(O(path),ZERO));SET(O(centers),TWO,J(O(path),ZERO));SET(O(centers),nb::int_(3),J(O(path),ZERO));P(triangles,NK("empty",ARGS(E(ac_literal(literals,7))),KW("dtype",ac_attr(ns.get("np"),"int32"))));SET(O(triangles),ZERO,V(E(ZERO),E(ONE),E(nb::int_(3))));SET(O(triangles),ONE,V(E(ONE),E(nb::int_(3)),E(TWO)));return T(E(O(centers)),E(NK("zeros",ARGS(E(ac_literal(literals,2))),KW("dtype",ac_attr(ns.get("np"),"float32")))),E(O(triangles)));}
 P(cos_limit,B(D(ONE,M(TWO,ac_power(D(O(limit),TWO),TWO))),nb::float_(1.0)));auto direction=ac_unpack(FN("_direction_vec_and_half_length",E(O(path)),E(O(closed))),2);P(direction_vectors,std::move(direction[0]));P(bevel_limit_array,std::move(direction[1]));
 P(point_count,FN("_calc_output_size",E(O(direction_vectors)),E(O(closed)),E(O(cos_limit)),E(O(bevel))));P(centers,NK("empty",ARGS(E(T(E(O(point_count)),E(TWO)))),KW("dtype",ac_attr(ns.get("np"),"float32"))));P(offsets,NK("empty",ARGS(E(T(E(O(point_count)),E(TWO)))),KW("dtype",ac_attr(ns.get("np"),"float32"))));P(triangles,NK("empty",ARGS(E(T(E(B(O(point_count),TWO)),E(nb::int_(3))))),KW("dtype",ac_attr(ns.get("np"),"int32"))));
 FN("_generate_2D_edge_meshes_loop",E(O(path)),E(O(closed)),E(O(cos_limit)),E(O(bevel)),E(O(direction_vectors)),E(O(bevel_limit_array)),E(O(centers)),E(O(offsets)),E(O(triangles)));FN("_normalize_triangle_orientation",E(O(triangles)),E(O(centers)),E(O(offsets)));return T(E(O(centers)),E(O(offsets)),E(O(triangles)));
}
// Compiled typed-container compatibility is separate from Python semantics.
// Source vertex_t remains the genuine scalar tuple. The canonical source dict
// physically encodes a native float tuple as its exact input bytes, matching
// Numba dictobject's no-NRT key memcmp, including distinct signed-zero keys.
nb::object ac_normalize_typed_key(nb::object tuple,bool compiled){
 if(!compiled)return tuple;if(!PyTuple_CheckExact(tuple.ptr())||PyTuple_GET_SIZE(tuple.ptr())!=2)return tuple;
 char bytes[16];Py_ssize_t used=0;for(Py_ssize_t i=0;i<2;++i){Py_buffer view{};PyObject* scalar=PyTuple_GET_ITEM(tuple.ptr(),i);if(PyObject_GetBuffer(scalar,&view,PyBUF_FORMAT|PyBUF_STRIDES)<0){PyErr_Clear();return tuple;}const char* f=view.format;const bool native=f&&(f[0]=='@'||f[0]=='=')?true:f&&f[0]!='<'&&f[0]!='>'&&f[0]!='!';if(f&&(f[0]=='@'||f[0]=='='))++f;const bool valid=native&&f&&f[1]=='\0'&&((f[0]=='f'&&view.itemsize==4)||(f[0]=='d'&&view.itemsize==8))&&view.len==view.itemsize&&view.buf&&!view.suboffsets;double value=0.0;if(valid){if(view.itemsize==4){float v;std::memcpy(&v,view.buf,4);value=v;}else std::memcpy(&value,view.buf,8);}if(!valid||!std::isfinite(value)||used+view.itemsize>16){PyBuffer_Release(&view);return tuple;}std::memcpy(bytes+used,view.buf,static_cast<size_t>(view.itemsize));used+=view.itemsize;PyBuffer_Release(&view);}return ac_check(PyBytes_FromStringAndSize(bytes,used));
}
nb::object ac_normalize_dict_key(nb::object tuple){return ac_normalize_typed_key(std::move(tuple),ac_active_typing!=nullptr);}
nb::object ac_dedup_compiled_return(nb::object result){
 if(ac_active_typing){nb::object input_type=nb::borrow<nb::object>(PyTuple_GET_ITEM(ac_active_typing->argument_types.ptr(),0));PyObject* raw=PyObject_GetAttrString(input_type.ptr(),"mutable");if(raw){nb::object mutable_value=nb::steal<nb::object>(raw);if(!ac_truth(mutable_value)){nb::object flags=ac_attr(result,"flags");if(PyObject_SetAttrString(flags.ptr(),"writeable",Py_False)<0)throw nb::python_error();}}else if(PyErr_ExceptionMatches(PyExc_AttributeError))PyErr_Clear();else throw nb::python_error();}return result;
}
struct AcNormalizeEdgeKey{int64_t a=0,b=0;};
bool ac_normalize_edge_equal(AcNormalizeEdgeKey a,AcNormalizeEdgeKey b){return a.a==b.a&&a.b==b.b;}
uint64_t ac_normalize_edge_hash(AcNormalizeEdgeKey key){
 constexpr uint64_t prime1=11400714785074694791ULL,prime2=14029467366897019727ULL,prime5=2870177450012600261ULL,modulus=(uint64_t(1)<<61)-1;uint64_t h=prime5;for(int64_t index:{key.a,key.b}){const uint64_t lane=static_cast<uint64_t>(index)%modulus;h+=lane*prime2;h=(h<<31)|(h>>33);h*=prime1;}h+=uint64_t(2)^(prime5^uint64_t(3527539));if(h==UINT64_MAX)h=1546275796ULL;if(h>=UINT64_MAX-1)h=static_cast<uint64_t>(int64_t(-43));return h;
}
// Each node is immutable history for the final list(edges) RHS only. There is
// no mutable payload array and no second authority during source set toggles.
// A slot read walks the newest preceding write; resize starts a new history
// and reinserts old live slots in Numba's ascending payload traversal order.
struct AcNormalizeProbeNode{const AcNormalizeProbeNode* const previous;const size_t slot;const uint64_t hash;const AcNormalizeEdgeKey key;const bool deleted;AcNormalizeProbeNode(const AcNormalizeProbeNode* p,size_t s,uint64_t h,AcNormalizeEdgeKey k,bool d):previous(p),slot(s),hash(h),key(k),deleted(d){}};
struct AcNormalizeTraversal{
 std::vector<std::unique_ptr<const AcNormalizeProbeNode>> history;const AcNormalizeProbeNode* head=nullptr;size_t size=16,used=0;
 static const AcNormalizeProbeNode* read(const AcNormalizeProbeNode* node,size_t slot){while(node&&node->slot!=slot)node=node->previous;return node;}
 std::pair<bool,size_t> lookup(AcNormalizeEdgeKey key,uint64_t hash,bool insertion)const{
  const size_t mask=size-1;size_t index=static_cast<size_t>(hash)&mask,free_slot=SIZE_MAX;uint64_t perturb=hash;auto check=[&](size_t slot,int& state){const auto* entry=read(head,slot);if(!entry){state=0;return false;}if(!entry->deleted&&entry->hash==hash&&ac_normalize_edge_equal(entry->key,key)){state=1;return true;}if(insertion&&entry->deleted&&free_slot==SIZE_MAX)free_slot=slot;state=2;return false;};
  int state=2;for(int linear=0;linear<3;++linear){if(check(index,state))return {true,index};if(state==0)return {false,insertion&&free_slot!=SIZE_MAX?free_slot:index};index=(index+1)&mask;}for(;;){if(check(index,state))return {true,index};if(state==0)return {false,insertion&&free_slot!=SIZE_MAX?free_slot:index};perturb>>=5;index=(index*5+1+static_cast<size_t>(perturb))&mask;}
 }
 void append(size_t slot,uint64_t hash,AcNormalizeEdgeKey key,bool deleted){history.emplace_back(new const AcNormalizeProbeNode(head,slot,hash,key,deleted));head=history.back().get();}
 void insert(AcNormalizeEdgeKey key,bool resize){const uint64_t hash=ac_normalize_edge_hash(key);const auto found=lookup(key,hash,true);if(found.first)return;append(found.second,hash,key,false);++used;if(resize&&used>=size/2){size_t next=size;if(used>SIZE_MAX/2){PyErr_SetString(PyExc_OverflowError,"typed set capacity overflow");throw nb::python_error();}do{if(next>SIZE_MAX/4){PyErr_SetString(PyExc_OverflowError,"typed set capacity overflow");throw nb::python_error();}next*=4;}while(used*2>=next);rebuild(next);}}
 void remove(AcNormalizeEdgeKey key){const uint64_t hash=ac_normalize_edge_hash(key);const auto found=lookup(key,hash,false);if(!found.first)throw nb::key_error("missing derived source edge");append(found.second,hash,key,true);--used;const size_t minimum=std::max(size_t(16),used*2);if(size>16&&minimum<=size/4){size_t next=size;while(next/2>=minimum)next/=2;rebuild(next);}}
 void rebuild(size_t next){const auto* old=head;const size_t old_size=size,old_used=used;head=nullptr;size=next;used=0;for(size_t slot=0;slot<old_size;++slot){const auto* entry=read(old,slot);if(entry&&!entry->deleted)insert(entry->key,false);}if(used!=old_used)throw nb::value_error("typed set traversal cardinality changed");}
 void toggle(AcNormalizeEdgeKey key){if(lookup(key,ac_normalize_edge_hash(key),false).first)remove(key);else insert(key,true);}
};
nb::object ac_normalize_ordered_edges(nb::object vertices,nb::object close,nb::object dictionary,nb::object edges,bool compiled){
 if(!compiled)return ac_check(PySequence_List(edges.ptr()));AcNormalizeTraversal traversal;int64_t previous=0;nb::object iterator=ac_iter(vertices);while(nb::object row=ac_next(iterator)){nb::object tuple=T(E(J(row,ZERO)),E(J(row,ONE))),key=ac_normalize_typed_key(tuple,true),index=ac_item(dictionary,key);const int64_t current=PyLong_AsLongLong(index.ptr());if(PyErr_Occurred())throw nb::python_error();traversal.toggle(previous<current?AcNormalizeEdgeKey{previous,current}:AcNormalizeEdgeKey{current,previous});previous=current;}iterator.reset();if(ac_truth(close)){nb::object last=J(vertices,nb::int_(-1)),tuple=T(E(J(last,ZERO)),E(J(last,ONE))),index=ac_item(dictionary,ac_normalize_typed_key(tuple,true));const int64_t current=PyLong_AsLongLong(index.ptr());if(PyErr_Occurred())throw nb::python_error();traversal.toggle({0,current});}traversal.remove({0,0});nb::list list;for(size_t slot=0;slot<traversal.size;++slot){const auto* entry=AcNormalizeTraversal::read(traversal.head,slot);if(entry&&!entry->deleted){nb::object key=T(E(nb::int_(entry->key.a)),E(nb::int_(entry->key.b)));const int exists=PySet_Contains(edges.ptr(),key.ptr());if(exists<0)throw nb::python_error();if(!exists)throw nb::value_error("derived traversal differs from canonical source set");list.append(key);}}if(PyList_GET_SIZE(list.ptr())!=PySet_Size(edges.ptr()))throw nb::value_error("derived traversal cardinality differs from source set");return list;
}
nb::object ac_normalize_edges_array(nb::object array,nb::object list,nb::object dtype,bool compiled){if(compiled&&PyList_Check(list.ptr())&&PyList_GET_SIZE(list.ptr())==0){PyErr_SetString(PyExc_IndexError,"getitem out of range");throw nb::python_error();}return ac_call(array,{E(list)},{{"dtype",E(dtype)}});}

DEF(shape_accel_numba_remove_duplicates){BEGIN;
 if(ac_truth(C(ac_len(ns,O(path)),TWO,Py_LE)))return O(path);P(dup_count,ZERO);nb::object iterator=ac_iter(FN("range",E(B(ac_len(ns,O(path)),TWO))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));if(ac_truth(NP("all",E(C(J(O(path),O(i)),J(O(path),A(O(i),ONE)),Py_EQ)))))P(dup_count,IA(O(dup_count),ONE));}iterator.reset();
 if(ac_truth(O(closed))&&ac_truth(NP("all",E(C(J(O(path),ZERO),J(O(path),nb::int_(-1)),Py_EQ)))))P(dup_count,IA(O(dup_count),ONE));if(ac_truth(C(O(dup_count),ZERO,Py_EQ)))return O(path);
 P(target_len,B(ac_len(ns,O(path)),O(dup_count)));P(new_path,NK("empty",ARGS(E(T(E(O(target_len)),E(J(ac_attr(O(path),"shape"),ONE))))),KW("dtype",ac_attr(O(path),"dtype"))));SET(O(new_path),ZERO,J(O(path),ZERO));P(index,ZERO);iterator=ac_iter(FN("range",E(ONE),E(ac_len(ns,O(path)))));
 while(nb::object value=ac_next(iterator)){P(i,std::move(value));if(ac_truth(C(O(index),B(O(target_len),ONE),Py_EQ)))break;if(ac_truth(NP("any",E(C(J(O(new_path),O(index)),J(O(path),O(i)),Py_NE))))){SET(O(new_path),A(O(index),ONE),J(O(path),O(i)));P(index,IA(O(index),ONE));}}iterator.reset();return ac_dedup_compiled_return(O(new_path));
}
DEF(shape_accel_numba_box){BEGIN;
 P(x_min,J(O(bounding_box),ac_literal(literals,1)));P(x_max,J(O(bounding_box),ac_literal(literals,2)));P(y_min,J(O(bounding_box),ac_literal(literals,3)));P(y_max,J(O(bounding_box),ac_literal(literals,4)));P(result,NK("empty",ARGS(E(ac_literal(literals,5))),KW("dtype",ac_attr(ns.get("np"),"float32"))));
 SET(O(result),ZERO,V(E(O(x_min)),E(O(y_min))));SET(O(result),ONE,V(E(D(A(O(x_min),O(x_max)),TWO)),E(O(y_min))));SET(O(result),TWO,V(E(O(x_max)),E(O(y_min))));SET(O(result),nb::int_(3),V(E(O(x_max)),E(D(A(O(y_min),O(y_max)),TWO))));SET(O(result),nb::int_(4),V(E(O(x_max)),E(O(y_max))));SET(O(result),nb::int_(5),V(E(D(A(O(x_min),O(x_max)),TWO)),E(O(y_max))));SET(O(result),nb::int_(6),V(E(O(x_min)),E(O(y_max))));SET(O(result),nb::int_(7),V(E(O(x_min)),E(D(A(O(y_min),O(y_max)),TWO))));SET(O(result),nb::int_(8),V(E(D(A(O(x_min),O(x_max)),TWO)),E(D(A(O(y_min),O(y_max)),TWO))));return O(result);
}

// Compiled source List() has an inferred Array element type even when empty.
// The public Python constructor is untyped until append; crossing declared
// ListType objmode requires the real typed NRT list at this source assignment.
// Python/py_func/disabled-JIT retain the genuine unchanged List() constructor.
nb::object ac_reconstruct_polygons_constructor(AccelerationGlobals& ns){
 nb::object list=ns.get("List");if(!ac_active_typing)return ac_call(list);
 nb::object empty=ac_attr(list,"empty_list"),types=ns.get("types"),argument=nb::borrow<nb::object>(PyTuple_GET_ITEM(ac_active_typing->argument_types.ptr(),0)),dtype=ac_attr(argument,"dtype"),element=ac_call(ac_attr(types,"Array"),{E(dtype),E(TWO),[]{return nb::str("C");}});return ac_call(empty,{E(element)});
}

DEF(shape_accel_numba_reconstruct){BEGIN;
 P(n_edges,J(ac_attr(O(edges),"shape"),ZERO));P(visited,NK("zeros",ARGS(E(O(n_edges))),KW("dtype",ac_attr(ns.get("np"),"bool_"))));P(n_vertices,J(ac_attr(O(vertices),"shape"),ZERO));P(incident,FN("List"));
 nb::object iterator=ac_iter(FN("range",E(O(n_vertices))));while(nb::object value=ac_next(iterator)){P(_,std::move(value));CALL(O(incident),"append",E(ac_call(ac_attr(ns.get("List"),"empty_list"),{E(ac_attr(ns.get("types"),"int64"))})));}iterator.reset();
 iterator=ac_iter(FN("range",E(O(n_edges))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));P(start,J(O(edges),T(E(O(i)),E(ZERO))));P(end,J(O(edges),T(E(O(i)),E(ONE))));CALL(J(O(incident),O(start)),"append",E(O(i)));CALL(J(O(incident),O(end)),"append",E(O(i)));}iterator.reset();P(polygons,ac_reconstruct_polygons_constructor(ns));
 iterator=ac_iter(FN("range",E(O(n_edges))));while(nb::object value=ac_next(iterator)){P(i,std::move(value));if(ac_truth(J(O(visited),O(i))))continue;P(poly,nb::list());P(start_v,J(O(edges),T(E(O(i)),E(ZERO))));P(current_v,J(O(edges),T(E(O(i)),E(ONE))));CALL(O(poly),"append",E(O(start_v)));CALL(O(poly),"append",E(O(current_v)));SET(O(visited),O(i),nb::bool_(true));P(closed,nb::bool_(false));
  while(!ac_truth(O(closed))){P(found,nb::bool_(false));nb::object incident=ac_iter(J(O(incident),O(current_v)));while(nb::object edge=ac_next(incident)){P(edge_idx,std::move(edge));if(ac_truth(J(O(visited),O(edge_idx))))continue;P(a,J(O(edges),T(E(O(edge_idx)),E(ZERO))));P(b,J(O(edges),T(E(O(edge_idx)),E(ONE))));P(next_v,ac_truth(C(O(b),O(current_v),Py_EQ))?O(a):O(b));SET(O(visited),O(edge_idx),nb::bool_(true));if(ac_truth(C(O(next_v),O(start_v),Py_EQ)))P(closed,nb::bool_(true));else CALL(O(poly),"append",E(O(next_v)));P(current_v,O(next_v));P(found,nb::bool_(true));if(ac_truth(C(O(current_v),O(start_v),Py_EQ)))P(closed,nb::bool_(true));break;}incident.reset();if(!ac_truth(O(found)))break;
  }P(polygon,J(O(vertices),NP("array",E(O(poly)))));CALL(O(polygons),"append",E(O(polygon)));
 }iterator.reset();return O(polygons);
}
DEF(shape_accel_numba_normalize){BEGIN;
 if(ac_truth(C(J(O(vertices),ac_literal(literals,1)),J(O(vertices),ac_literal(literals,2)),Py_EQ))&&ac_truth(C(J(O(vertices),ac_literal(literals,3)),J(O(vertices),ac_literal(literals,4)),Py_EQ))){P(vertices,J(O(vertices),ac_slice(nb::none(),nb::int_(-1))));P(close,nb::bool_(true));}
 P(vertex_to_idx,nb::dict());P(new_vertices,nb::list());P(edges,FN("set"));P(prev_idx,ZERO);P(i,ZERO);nb::object iterator=ac_iter(O(vertices));
 while(nb::object value=ac_next(iterator)){P(vertex,std::move(value));P(vertex_t,T(E(J(O(vertex),ZERO)),E(J(O(vertex),ONE))));nb::object dictionary_key=ac_normalize_dict_key(O(vertex_t));int exists=PySequence_Contains(O(vertex_to_idx).ptr(),dictionary_key.ptr());if(exists<0)throw nb::python_error();if(exists)P(current_idx,J(O(vertex_to_idx),dictionary_key));else{P(current_idx,O(i));SET(O(vertex_to_idx),dictionary_key,O(i));CALL(O(new_vertices),"append",E(O(vertex)));P(i,IA(O(i),ONE));}
  P(edge,ac_truth(C(O(prev_idx),O(current_idx),Py_LT))?T(E(O(prev_idx)),E(O(current_idx))):T(E(O(current_idx)),E(O(prev_idx))));exists=PySequence_Contains(O(edges).ptr(),O(edge).ptr());if(exists<0)throw nb::python_error();CALL(O(edges),exists?"remove":"add",E(O(edge)));P(prev_idx,O(current_idx));
 }iterator.reset();if(ac_truth(O(close))){P(vertex_t,T(E(J(O(vertices),ac_literal(literals,2))),E(J(O(vertices),ac_literal(literals,4)))));P(idx,J(O(vertex_to_idx),ac_normalize_dict_key(O(vertex_t))));P(edge,T(E(ZERO),E(O(idx))));int exists=PySequence_Contains(O(edges).ptr(),O(edge).ptr());if(exists<0)throw nb::python_error();CALL(O(edges),exists?"remove":"add",E(O(edge)));}
 if(!ac_active_typing){CALL(O(edges),"remove",E(ac_literal(literals,1)));}else{nb::object remove_function=ac_attr(O(edges),"remove");nb::object self_edge=ac_literal(literals,1);const int exists=PySet_Contains(O(edges).ptr(),self_edge.ptr());if(exists<0)throw nb::python_error();if(!exists){PyErr_SetString(PyExc_KeyError,"set.remove(): key not in set");throw nb::python_error();}ac_call(remove_function,{E(self_edge)});}P(new_vertices_array,NK("empty",ARGS(E(T(E(ac_len(ns,O(new_vertices))),E(TWO)))),KW("dtype",ac_attr(ns.get("np"),"float32"))));iterator=ac_iter(FN("enumerate",E(O(new_vertices))));while(nb::object value=ac_next(iterator)){auto pair=ac_unpack(std::move(value),2);P(i,std::move(pair[0]));P(vertex,std::move(pair[1]));SET(O(new_vertices_array),O(i),O(vertex));}iterator.reset();if(!ac_active_typing){P(edges_array,NK("array",ARGS(E(FN("list",E(O(edges))))),KW("dtype",ac_attr(ns.get("np"),"int64"))));}else{nb::object array_function=ns.np("array");nb::object edge_list=ac_normalize_ordered_edges(O(vertices),O(close),O(vertex_to_idx),O(edges),true);nb::object edge_dtype=ac_attr(ns.get("np"),"int64");P(edges_array,ac_normalize_edges_array(array_function,edge_list,edge_dtype,true));}return T(E(O(new_vertices_array)),E(O(edges_array)));
}

using AccelerationEngine=nb::object(*)(nb::dict,nb::tuple,ShapeAccelerationLocals&);
struct NumbaAccelerationEntry{const char* name;AccelerationEngine engine;std::vector<const char*> parameters;};
const NumbaAccelerationEntry ac_numba_entries[]={
 {"_dot",shape_accel_numba_dot,{"v0","v1"}},
 {"_cross_z",shape_accel_numba_cross,{"v0","v1"}},
 {"_sign_abs",shape_accel_numba_sign_abs,{"f"}},
 {"_orthogonal_vector",shape_accel_numba_orthogonal,{"v","ccw"}},
 {"_calc_output_size",shape_accel_numba_output_size,{"direction_vectors","closed","cos_miter_limit","bevel"}},
 {"_set_centers_and_offsets",shape_accel_numba_set_centers,{"centers","offsets","triangles","vertex","vec1","vec2","half_vec1_len","half_vec2_len","j","cos_limit","always_bevel"}},
 {"_normalize_triangle_orientation",shape_accel_numba_normalize_triangles,{"triangles","centers","offsets"}},
 {"_orientation",shape_accel_numba_orientation,{"p1","p2","p3"}},
 {"_are_polar_angles_monotonic",shape_accel_numba_monotonic,{"vertices","centroid"}},
 {"is_convex",shape_accel_numba_convex,{"vertices"}},
 {"_direction_vec_and_half_length",shape_accel_numba_directions,{"path","closed"}},
 {"_generate_2D_edge_meshes_loop",shape_accel_numba_edges_loop,{"path","closed","cos_limit","bevel","direction_vectors","bevel_limit_array","centers","offsets","triangles"}},
 {"_cut_end_if_repetition",shape_accel_numba_cut_end,{"path"}},
 {"generate_2D_edge_meshes",shape_accel_numba_edges,{"path","closed","limit","bevel"}},
 {"remove_path_duplicates",shape_accel_numba_remove_duplicates,{"path","closed"}},
 {"create_box_from_bounding",shape_accel_numba_box,{"bounding_box"}},
 {"reconstruct_polygons_from_edges",shape_accel_numba_reconstruct,{"vertices","edges"}},
 {"normalize_vertices_and_edges",shape_accel_numba_normalize,{"vertices","close"}}
};
const NumbaAccelerationEntry& ac_entry(const std::string& name){for(const auto& entry:ac_numba_entries)if(name==entry.name)return entry;throw nb::value_error("unknown native Numba acceleration operation");}

// Immutable, versioned specialization metadata. An old cached tuple of Numba
// argument types never has a plain Unicode version tag and remains supported.
constexpr const char* ac_scalar_float_plan_tag="shape_accel.scalar_float.v1";
struct AccelerationScalarFloatPlan{
 int left_bytes=0,right_bytes=0,product_bytes=0,reduction_bytes=0;
 nb::object arguments,ndarray_type;
};
int ac_scalar_plan_width(nb::handle value){
 if(!PyLong_CheckExact(value.ptr()))throw nb::type_error("native scalar typing width must be an exact integer");
 long width=PyLong_AsLong(value.ptr());if(PyErr_Occurred())throw nb::python_error();
 if(width!=4&&width!=8)throw nb::value_error("native scalar typing width must be 4 or 8");return static_cast<int>(width);
}
bool ac_scalar_float_plan_decode(nb::handle value,AccelerationScalarFloatPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 PyObject* tag=PyTuple_GET_ITEM(value.ptr(),0);int equal=PyUnicode_CompareWithASCIIString(tag,ac_scalar_float_plan_tag);if(PyErr_Occurred())throw nb::python_error();
 if(equal!=0)throw nb::value_error("unsupported native scalar typing plan version");
 if(PyTuple_GET_SIZE(value.ptr())!=4)throw nb::type_error("native scalar typing plan requires four metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=2||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=4||!PyType_Check(ndarray_type))throw nb::type_error("malformed native scalar typing plan");
 plan.left_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,0)));plan.right_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,1)));
 plan.product_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,2)));plan.reduction_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,3)));
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);return true;
}
// NumPy exports the actual logical first pointer and signed byte strides. No
// normalization, allocation, writable request, data copy or scalar boxing.
struct AccelerationScalarBuffer{
 Py_buffer view{};bool acquired=false;
 AccelerationScalarBuffer()=default;AccelerationScalarBuffer(const AccelerationScalarBuffer&)=delete;AccelerationScalarBuffer& operator=(const AccelerationScalarBuffer&)=delete;
 ~AccelerationScalarBuffer(){if(acquired)PyBuffer_Release(&view);}
 void acquire(nb::handle object){if(PyObject_GetBuffer(object.ptr(),&view,PyBUF_FORMAT|PyBUF_STRIDES)<0)throw nb::python_error();acquired=true;}
 bool matches(int width)const{
  if(view.ndim!=1||!view.shape||view.shape[0]<2||!view.strides||!view.buf||view.suboffsets||view.itemsize!=width||!view.format)return false;
  const char* format=view.format;
  if(*format=='@'||*format=='=')++format;
  else if(*format=='<'||*format=='>'||*format=='!'){
   const uint16_t endian=1;const bool little=*reinterpret_cast<const unsigned char*>(&endian)==1;
   if((*format=='<')!=little)return false;++format;
  }
  return format[0]==(width==4?'f':'d')&&format[1]=='\0';
 }
 double read(Py_ssize_t index,int width)const{
  const char* address=static_cast<const char*>(view.buf)+index*view.strides[0];
  if(width==4){float value;std::memcpy(&value,address,sizeof(value));return static_cast<double>(value);}
  double value;std::memcpy(&value,address,sizeof(value));return value;
 }
};
double ac_scalar_float_product(double left,double right,int width){
 // Separate volatile stores prohibit contraction/reassociation and commit the
 // same float32 rounding points as Numba's non-fastmath scalar operations.
 if(width==4){const float a=static_cast<float>(left),b=static_cast<float>(right);volatile float result=a*b;return static_cast<double>(result);}
 volatile double result=left*right;return result;
}
double ac_scalar_float_reduce(double left,double right,int width,bool subtract){
 if(width==4){const float a=static_cast<float>(left),b=static_cast<float>(right);volatile float result=subtract?a-b:a+b;return static_cast<double>(result);}
 volatile double result=subtract?left-right:left+right;return result;
}
bool ac_scalar_float_fast(const char* operation,const AccelerationScalarFloatPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 static_assert(sizeof(float)==4&&sizeof(double)==8,"native scalar policy requires binary32/binary64 storage");
 nb::object left=owner.get("v0"),right=owner.get("v1");
 if(reinterpret_cast<PyObject*>(Py_TYPE(left.ptr()))!=plan.ndarray_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(right.ptr()))!=plan.ndarray_type.ptr())return false;
 AccelerationScalarBuffer a,b;a.acquire(left);b.acquire(right);if(!a.matches(plan.left_bytes)||!b.matches(plan.right_bytes))return false;
 const bool subtract=std::strcmp(operation,"_cross_z")==0;
 const double first_left=a.read(0,plan.left_bytes);const double first_right=b.read(subtract?1:0,plan.right_bytes);
 const double first=ac_scalar_float_product(first_left,first_right,plan.product_bytes);
 const double second_left=a.read(1,plan.left_bytes);const double second_right=b.read(subtract?0:1,plan.right_bytes);
 const double second=ac_scalar_float_product(second_left,second_right,plan.product_bytes);
 // Python float is transport only: objmode unboxes it to the genuine resolved
 // Numba scalar return type; generic py_func results still use their old path.
 result=ac_check(PyFloat_FromDouble(ac_scalar_float_reduce(first,second,plan.reduction_bytes,subtract)));return true;
}
// Orientation has a separate immutable protocol; scalar-v1 stays unchanged.
constexpr const char* ac_orientation_float_plan_tag="shape_accel.orientation_float.v1";
struct AccelerationOrientationFloatPlan{
 int input1_bytes=0,input2_bytes=0,input3_bytes=0,sub21_bytes=0,sub31_bytes=0,product_bytes=0,reduction_bytes=0;
 nb::object arguments,ndarray_type,enum_type,clockwise,anticlockwise,collinear,val_constructor;
};
bool ac_orientation_float_plan_decode(nb::handle value,AccelerationOrientationFloatPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_orientation_float_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native orientation typing plan requires seven metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);
 PyObject* enum_type=PyTuple_GET_ITEM(value.ptr(),4);PyObject* members=PyTuple_GET_ITEM(value.ptr(),5);PyObject* val_constructor=PyTuple_GET_ITEM(value.ptr(),6);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=3||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=7||!PyType_Check(ndarray_type)||!PyType_Check(enum_type)||!PyTuple_CheckExact(members)||PyTuple_GET_SIZE(members)!=3||!PyType_Check(val_constructor))throw nb::type_error("malformed native orientation typing plan");
 for(Py_ssize_t i=0;i<3;++i)if(reinterpret_cast<PyObject*>(Py_TYPE(PyTuple_GET_ITEM(members,i)))!=enum_type)throw nb::type_error("orientation plan requires genuine members of its enum type");
 plan.input1_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,0)));plan.input2_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,1)));plan.input3_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,2)));
 plan.sub21_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,3)));plan.sub31_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,4)));plan.product_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,5)));plan.reduction_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,6)));
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.enum_type=nb::borrow<nb::object>(enum_type);
 plan.clockwise=nb::borrow<nb::object>(PyTuple_GET_ITEM(members,0));plan.anticlockwise=nb::borrow<nb::object>(PyTuple_GET_ITEM(members,1));plan.collinear=nb::borrow<nb::object>(PyTuple_GET_ITEM(members,2));plan.val_constructor=nb::borrow<nb::object>(val_constructor);return true;
}
bool ac_orientation_float_fast(const AccelerationOrientationFloatPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object p1=owner.get("p1"),p2=owner.get("p2"),p3=owner.get("p3");
 if(reinterpret_cast<PyObject*>(Py_TYPE(p1.ptr()))!=plan.ndarray_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(p2.ptr()))!=plan.ndarray_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(p3.ptr()))!=plan.ndarray_type.ptr())return false;
 AccelerationScalarBuffer a,b,c;a.acquire(p1);b.acquire(p2);c.acquire(p3);
 if(!a.matches(plan.input1_bytes)||!b.matches(plan.input2_bytes)||!c.matches(plan.input3_bytes))return false;
 // Exact source expression order, with each committed compiler precision.
 const double first21_left=b.read(0,plan.input2_bytes);
 const double first21_right=a.read(0,plan.input1_bytes);
 const double first21=ac_scalar_float_reduce(first21_left,first21_right,plan.sub21_bytes,true);
 const double first31_left=c.read(1,plan.input3_bytes);
 const double first31_right=a.read(1,plan.input1_bytes);
 const double first31=ac_scalar_float_reduce(first31_left,first31_right,plan.sub31_bytes,true);
 const double first=ac_scalar_float_product(first21,first31,plan.product_bytes);
 const double second21_left=b.read(1,plan.input2_bytes);
 const double second21_right=a.read(1,plan.input1_bytes);
 const double second21=ac_scalar_float_reduce(second21_left,second21_right,plan.sub21_bytes,true);
 const double second31_left=c.read(0,plan.input3_bytes);
 const double second31_right=a.read(0,plan.input1_bytes);
 const double second31=ac_scalar_float_reduce(second31_left,second31_right,plan.sub31_bytes,true);
 const double second=ac_scalar_float_product(second21,second31,plan.product_bytes);
 const double value=ac_scalar_float_reduce(first,second,plan.reduction_bytes,true);
 // Keep the original assigned source val lease/type before either branch.
 owner.put("val",ac_call(plan.val_constructor,{[&]{return nb::float_(value);}}));
 if(value<0.0)result=plan.clockwise;else if(value>0.0)result=plan.anticlockwise;else result=plan.collinear;return true;
}
// Centers use a distinct immutable compiler plan; the previous plans stay exact.
constexpr const char* ac_centers_float_plan_tag="shape_accel.centers_float.v1";
struct AccelerationCentersFloatPlan{
 std::vector<int> widths;
 nb::object arguments,ndarray_type,dot_constructor,cross_constructor,scale_constructor,float64_constructor,miter_dtype,empty;
};
bool ac_centers_float_plan_decode(nb::handle value,AccelerationCentersFloatPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_centers_float_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=6)throw nb::type_error("native centers typing plan requires six metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* constructors=PyTuple_GET_ITEM(value.ptr(),4);PyObject* primitives=PyTuple_GET_ITEM(value.ptr(),5);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=11||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=24||!PyType_Check(ndarray_type)||!PyTuple_CheckExact(constructors)||PyTuple_GET_SIZE(constructors)!=4||!PyTuple_CheckExact(primitives)||PyTuple_GET_SIZE(primitives)!=2||!PyCFunction_Check(PyTuple_GET_ITEM(primitives,1)))throw nb::type_error("malformed native centers typing plan");
 for(Py_ssize_t i=0;i<4;++i)if(!PyType_Check(PyTuple_GET_ITEM(constructors,i)))throw nb::type_error("centers plan requires genuine scalar constructors");
 for(Py_ssize_t i=0;i<24;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.dot_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,0));plan.cross_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,1));plan.scale_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,2));plan.float64_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,3));plan.miter_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,0));plan.empty=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,1));return true;
}
struct AccelerationCentersBuffer{
 Py_buffer view{};bool acquired=false;
 AccelerationCentersBuffer()=default;AccelerationCentersBuffer(const AccelerationCentersBuffer&)=delete;AccelerationCentersBuffer& operator=(const AccelerationCentersBuffer&)=delete;
 ~AccelerationCentersBuffer(){if(acquired)PyBuffer_Release(&view);}
 void acquire(nb::handle object){if(PyObject_GetBuffer(object.ptr(),&view,PyBUF_FORMAT|PyBUF_STRIDES)<0)throw nb::python_error();acquired=true;}
 bool format(int width,bool integer)const{
  if(!view.buf||!view.shape||!view.strides||view.suboffsets||view.itemsize!=width||!view.format)return false;
  const char* f=view.format;if(*f=='@'||*f=='=')++f;else if(*f=='<'||*f=='>'||*f=='!'){const uint16_t endian=1;const bool little=*reinterpret_cast<const unsigned char*>(&endian)==1;if((*f=='<')!=little)return false;++f;}
  if(integer)return f[1]=='\0'&&(width==4?(f[0]=='i'||f[0]=='l'):(f[0]=='q'||f[0]=='l'));
  return f[0]==(width==4?'f':'d')&&f[1]=='\0';
 }
 bool output(int width,bool integer,int columns)const{return view.ndim==2&&format(width,integer)&&!view.readonly&&view.shape[0]>=3&&view.shape[1]==columns&&(PyBuffer_IsContiguous(&view,'C')||PyBuffer_IsContiguous(&view,'F'));}
 bool input(int width)const{return view.ndim==1&&format(width,false)&&view.shape[0]==2;}
 bool span(uintptr_t& first,uintptr_t& last)const{
  uintptr_t low=0,high=0;
  for(int i=0;i<view.ndim;++i){if(view.shape[i]<=0)return false;const uintptr_t n=static_cast<uintptr_t>(view.shape[i]-1);const Py_ssize_t stride=view.strides[i];const uintptr_t step=stride<0?static_cast<uintptr_t>(-(stride+1))+1:static_cast<uintptr_t>(stride);if(n&&step>UINTPTR_MAX/n)return false;const uintptr_t delta=n*step;uintptr_t& end=stride<0?low:high;if(delta>UINTPTR_MAX-end)return false;end+=delta;}
  const uintptr_t pointer=reinterpret_cast<uintptr_t>(view.buf);if(pointer<low||high>UINTPTR_MAX-pointer||static_cast<uintptr_t>(view.itemsize)>UINTPTR_MAX-(pointer+high))return false;first=pointer-low;last=pointer+high+static_cast<uintptr_t>(view.itemsize);return true;
 }
 char* address(Py_ssize_t row,Py_ssize_t column=0)const{return static_cast<char*>(view.buf)+row*view.strides[0]+(view.ndim==2?column*view.strides[1]:0);}
 double read(Py_ssize_t row,int width)const{if(width==4){float value;std::memcpy(&value,address(row),4);return value;}double value;std::memcpy(&value,address(row),8);return value;}
 void write_float(Py_ssize_t row,Py_ssize_t column,double value,int width)const{if(width==4){float stored=static_cast<float>(value);std::memcpy(address(row,column),&stored,4);}else std::memcpy(address(row,column),&value,8);}
 void write_integer(Py_ssize_t row,Py_ssize_t column,int64_t value,int width)const{if(width==4){uint32_t stored=static_cast<uint32_t>(value);std::memcpy(address(row,column),&stored,4);}else std::memcpy(address(row,column),&value,8);}
};
double ac_centers_float_cast(double value,int width){if(width==4){volatile float result=static_cast<float>(value);return result;}return value;}
double ac_centers_float_divide(double left,double right,int width){if(width==4){const float a=static_cast<float>(left),b=static_cast<float>(right);volatile float result=a/b;return result;}volatile double result=left/right;return result;}
bool ac_centers_float_fast(const AccelerationCentersFloatPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 const auto& w=plan.widths;
 nb::object centers=owner.get("centers"),offsets=owner.get("offsets"),triangles=owner.get("triangles"),vertex=owner.get("vertex"),vec1=owner.get("vec1"),vec2=owner.get("vec2");
 for(nb::handle value:{nb::handle(centers),nb::handle(offsets),nb::handle(triangles),nb::handle(vertex),nb::handle(vec1),nb::handle(vec2)})if(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))!=plan.ndarray_type.ptr())return false;
 nb::object j_value=owner.get("j"),always_bevel=owner.get("always_bevel"),half1=owner.get("half_vec1_len"),half2=owner.get("half_vec2_len"),limit=owner.get("cos_limit");
 if(!PyLong_CheckExact(j_value.ptr())||!PyBool_Check(always_bevel.ptr())||!PyFloat_CheckExact(half1.ptr())||!PyFloat_CheckExact(half2.ptr())||!PyFloat_CheckExact(limit.ptr()))return false;
 int overflow=0;const int64_t j=PyLong_AsLongLongAndOverflow(j_value.ptr(),&overflow);if(PyErr_Occurred())throw nb::python_error();if(overflow||j<0||j>PY_SSIZE_T_MAX-4)return false;
 const double half_vec1_len=ac_centers_float_cast(PyFloat_AS_DOUBLE(half1.ptr()),w[6]),half_vec2_len=ac_centers_float_cast(PyFloat_AS_DOUBLE(half2.ptr()),w[7]),cos_limit=ac_centers_float_cast(PyFloat_AS_DOUBLE(limit.ptr()),w[8]);
 AccelerationCentersBuffer c,o,t,v,a,b;c.acquire(centers);o.acquire(offsets);t.acquire(triangles);v.acquire(vertex);a.acquire(vec1);b.acquire(vec2);
 if(!c.output(w[0],false,2)||!o.output(w[1],false,2)||!t.output(w[2],true,3)||!v.input(w[3])||!a.input(w[4])||!b.input(w[5]))return false;
 const AccelerationCentersBuffer* storage[]={&c,&o,&t,&v,&a,&b};uintptr_t first[6],last[6];for(size_t i=0;i<6;++i)if(!storage[i]->span(first[i],last[i]))return false;
 // Every potentially written array must be disjoint before any source writes.
 for(size_t i=0;i<3;++i)for(size_t k=i+1;k<6;++k)if(first[i]<last[k]&&first[k]<last[i])return false;
 // Nonfinite payloads keep the exact generic stores and primitive semantics.
 if(!std::isfinite(half_vec1_len)||!std::isfinite(half_vec2_len)||!std::isfinite(cos_limit))return false;
 for(Py_ssize_t k=0;k<2;++k)if(!std::isfinite(v.read(k,w[3]))||!std::isfinite(a.read(k,w[4]))||!std::isfinite(b.read(k,w[5])))return false;
 // The same eight original dot/cross loads and committed product/reduction order.
 const double dot_first_left=a.read(0,w[4]),dot_first_right=b.read(0,w[5]);const double dot_first=ac_scalar_float_product(dot_first_left,dot_first_right,w[9]);
 const double dot_second_left=a.read(1,w[4]),dot_second_right=b.read(1,w[5]);const double dot_second=ac_scalar_float_product(dot_second_left,dot_second_right,w[9]);
 const double cos_angle=ac_scalar_float_reduce(dot_first,dot_second,w[10],false);
 // Pure admission decision; the source bevel local/branch remains below.
 const Py_ssize_t capacity_count=2+static_cast<Py_ssize_t>(always_bevel.ptr()==Py_True||cos_angle<cos_limit);
 if(j>c.view.shape[0]-capacity_count||j>o.view.shape[0]-capacity_count||j>t.view.shape[0]-capacity_count)return false;
 owner.put("cos_angle",ac_call(plan.dot_constructor,{[&]{return nb::float_(cos_angle);}}));
 const double cross_first_left=a.read(0,w[4]),cross_first_right=b.read(1,w[5]);const double cross_first=ac_scalar_float_product(cross_first_left,cross_first_right,w[9]);
 const double cross_second_left=a.read(1,w[4]),cross_second_right=b.read(0,w[5]);const double cross_second=ac_scalar_float_product(cross_second_left,cross_second_right,w[9]);
 const double sin_angle=ac_scalar_float_reduce(cross_first,cross_second,w[11],true);owner.put("sin_angle",ac_call(plan.cross_constructor,{[&]{return nb::float_(sin_angle);}}));
 const bool bevel=always_bevel.ptr()==Py_True||cos_angle<cos_limit;owner.put("bevel",nb::bool_(bevel));const int count=2+static_cast<int>(bevel);
 for(int64_t i=0;i<count;++i){owner.put("i",nb::int_(i));for(Py_ssize_t k=0;k<2;++k)c.write_float(static_cast<Py_ssize_t>(j+i),k,v.read(k,w[3]),w[0]);}
 double miter_values[2];
 if(sin_angle==0.0){const double first_value=ac_centers_float_cast(a.read(1,w[4]),4);const double second_value=ac_centers_float_cast(-a.read(0,w[4]),4);miter_values[0]=ac_centers_float_cast(ac_scalar_float_product(first_value,0.5,w[19]),w[20]);miter_values[1]=ac_centers_float_cast(ac_scalar_float_product(second_value,0.5,w[19]),w[20]);}
 else{
  double scale_factor=ac_centers_float_divide(1.0,sin_angle,w[12]);owner.put("scale_factor",ac_call(plan.scale_constructor,{[&]{return nb::float_(scale_factor);}}));
  if(bevel){double sign,mag;if(scale_factor>0.0){sign=1.0;mag=scale_factor;}else if(scale_factor<0.0){sign=-1.0;mag=-scale_factor;}else{sign=0.0;mag=0.0;}owner.put("sign",ac_call(plan.float64_constructor,{[&]{return nb::float_(sign);}}));owner.put("mag",ac_call(plan.float64_constructor,{[&]{return nb::float_(mag);}}));double minimum=ac_centers_float_cast(mag,w[13]);const double first_length=ac_centers_float_cast(half_vec1_len,w[13]);if(first_length<minimum)minimum=first_length;const double second_length=ac_centers_float_cast(half_vec2_len,w[13]);if(second_length<minimum)minimum=second_length;scale_factor=ac_scalar_float_product(sign,minimum,w[14]);owner.put("scale_factor",ac_call(plan.scale_constructor,{[&]{return nb::float_(scale_factor);}}));}
  for(Py_ssize_t k=0;k<2;++k){const double left=a.read(k,w[4]),right=b.read(k,w[5]);const double difference=ac_scalar_float_reduce(left,right,w[15],true);const double half=ac_scalar_float_product(difference,0.5,w[16]);miter_values[k]=ac_centers_float_cast(ac_scalar_float_product(half,scale_factor,w[17]),w[18]);}
 }
 // This is the original assigned local: a real ndarray, never a state mirror.
 nb::object miter=ac_call(plan.empty,{[]{return nb::int_(2);}},{{"dtype",[&]{return plan.miter_dtype;}}});AccelerationCentersBuffer m;m.acquire(miter);if(!m.input(w[18]))throw nb::type_error("native centers miter allocation changed storage");for(Py_ssize_t k=0;k<2;++k)m.write_float(k,0,miter_values[k],w[18]);owner.put("miter",miter);
 auto triangle=[&](Py_ssize_t row,int64_t x,int64_t y,int64_t z){t.write_integer(row,0,x,w[2]);t.write_integer(row,1,y,w[2]);t.write_integer(row,2,z,w[2]);};
 if(bevel){
  const bool clockwise=sin_angle<0.0,counterclockwise=!clockwise;owner.put("clockwise",nb::bool_(clockwise));owner.put("counterclockwise",nb::bool_(counterclockwise));const double invert=counterclockwise?-1.0:1.0;owner.put("invert",nb::float_(invert));
  for(Py_ssize_t k=0;k<2;++k)o.write_float(static_cast<Py_ssize_t>(j+counterclockwise),k,ac_scalar_float_product(invert,m.read(k,w[18]),w[21]),w[1]);
  const double orth1_first=counterclockwise?a.read(1,w[4]):-a.read(1,w[4]);const double orth1_second=counterclockwise?-a.read(0,w[4]):a.read(0,w[4]);o.write_float(static_cast<Py_ssize_t>(j+clockwise),0,ac_scalar_float_product(0.5,orth1_first,w[22]),w[1]);o.write_float(static_cast<Py_ssize_t>(j+clockwise),1,ac_scalar_float_product(0.5,orth1_second,w[22]),w[1]);
  const double orth2_first=counterclockwise?b.read(1,w[5]):-b.read(1,w[5]);const double orth2_second=counterclockwise?-b.read(0,w[5]):b.read(0,w[5]);o.write_float(static_cast<Py_ssize_t>(j+2),0,ac_scalar_float_product(0.5,orth2_first,w[23]),w[1]);o.write_float(static_cast<Py_ssize_t>(j+2),1,ac_scalar_float_product(0.5,orth2_second,w[23]),w[1]);
  triangle(static_cast<Py_ssize_t>(j),j,j+1,j+2);triangle(static_cast<Py_ssize_t>(j+1),j+counterclockwise,j+2,j+3);triangle(static_cast<Py_ssize_t>(j+2),j+1+clockwise,j+3,j+4);result=nb::int_(3);return true;
 }
 for(Py_ssize_t k=0;k<2;++k)o.write_float(static_cast<Py_ssize_t>(j),k,m.read(k,w[18]),w[1]);for(Py_ssize_t k=0;k<2;++k)o.write_float(static_cast<Py_ssize_t>(j+1),k,-m.read(k,w[18]),w[1]);triangle(static_cast<Py_ssize_t>(j),j,j+1,j+2);triangle(static_cast<Py_ssize_t>(j+1),j+1,j+2,j+3);result=nb::int_(2);return true;
}
// Directions retain only immutable genuine compiler and external primitive refs.
constexpr const char* ac_directions_float_plan_tag="shape_accel.directions_float.v1";
struct AccelerationDirectionsFloatPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,order,float32_dtype,difference_dtype,half_dtype,empty_like,empty;
};
bool ac_directions_float_plan_decode(nb::handle value,AccelerationDirectionsFloatPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_directions_float_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native directions typing plan requires seven metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* order=PyTuple_GET_ITEM(value.ptr(),4);PyObject* dtypes=PyTuple_GET_ITEM(value.ptr(),5);PyObject* primitives=PyTuple_GET_ITEM(value.ptr(),6);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=2||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=10||!PyType_Check(ndarray_type)||!PyUnicode_CheckExact(order)||!PyTuple_CheckExact(dtypes)||PyTuple_GET_SIZE(dtypes)!=3||!PyTuple_CheckExact(primitives)||PyTuple_GET_SIZE(primitives)!=2||!PyCallable_Check(PyTuple_GET_ITEM(primitives,0))||!PyCFunction_Check(PyTuple_GET_ITEM(primitives,1)))throw nb::type_error("malformed native directions typing plan");
 int is_c=PyUnicode_CompareWithASCIIString(order,"C"),is_f=PyUnicode_CompareWithASCIIString(order,"F");if(PyErr_Occurred())throw nb::python_error();if(is_c!=0&&is_f!=0)throw nb::value_error("native directions allocation order must be C or F");
 for(Py_ssize_t i=0;i<10;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.order=nb::borrow<nb::object>(order);plan.float32_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(dtypes,0));plan.difference_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(dtypes,1));plan.half_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(dtypes,2));plan.empty_like=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,0));plan.empty=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,1));return true;
}
double ac_directions_read2(const AccelerationCentersBuffer& buffer,Py_ssize_t row,Py_ssize_t column,int width){if(width==4){float value;std::memcpy(&value,buffer.address(row,column),4);return value;}double value;std::memcpy(&value,buffer.address(row,column),8);return value;}
double ac_directions_sqrt(double value,int width){if(width==4){const float input=static_cast<float>(value);volatile float result=std::sqrt(input);return result;}volatile double result=std::sqrt(value);return result;}
double ac_directions_length(double x,double y,const std::vector<int>& w){const double square0=ac_scalar_float_product(x,x,w[3]);const double square1=ac_scalar_float_product(y,y,w[3]);const double sum=ac_scalar_float_reduce(square0,square1,w[4],false);return ac_directions_sqrt(sum,w[5]);}
bool ac_directions_allocated2(const AccelerationCentersBuffer& buffer,Py_ssize_t rows,int width,nb::handle ndarray_type,nb::handle value){return reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))==ndarray_type.ptr()&&buffer.view.ndim==2&&buffer.format(width,false)&&!buffer.view.readonly&&buffer.view.shape[0]==rows&&buffer.view.shape[1]==2&&(PyBuffer_IsContiguous(&buffer.view,'C')||PyBuffer_IsContiguous(&buffer.view,'F'));}
bool ac_directions_allocated1(const AccelerationCentersBuffer& buffer,Py_ssize_t rows,int width,nb::handle ndarray_type,nb::handle value){return reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))==ndarray_type.ptr()&&buffer.view.ndim==1&&buffer.format(width,false)&&!buffer.view.readonly&&buffer.view.shape[0]==rows&&PyBuffer_IsContiguous(&buffer.view,'C');}
bool ac_directions_float_fast(const AccelerationDirectionsFloatPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 const auto& w=plan.widths;nb::object path=owner.get("path"),closed_value=owner.get("closed");
 if(reinterpret_cast<PyObject*>(Py_TYPE(path.ptr()))!=plan.ndarray_type.ptr()||!PyBool_Check(closed_value.ptr()))return false;
 AccelerationCentersBuffer p;p.acquire(path);if(p.view.ndim!=2||!p.format(w[0],false)||p.view.shape[0]<0||p.view.shape[1]!=2)return false;
 const Py_ssize_t n=p.view.shape[0];const bool closed=closed_value.ptr()==Py_True;const Py_ssize_t start=closed?0:1;
 // Pure admission arithmetic precedes all source allocations, locals and writes.
 // Exceptional zero/nonfinite/length overflow/underflow cases keep exact generic
 // source primitives; there is no fallback after a partially written output.
 for(Py_ssize_t i=start;i<n;++i){const Py_ssize_t previous=i==0?n-1:i-1;double difference[2];for(Py_ssize_t k=0;k<2;++k){const double left=ac_directions_read2(p,i,k,w[0]),right=ac_directions_read2(p,previous,k,w[0]);if(!std::isfinite(left)||!std::isfinite(right))return false;difference[k]=ac_centers_float_cast(ac_scalar_float_reduce(left,right,w[1],true),w[2]);}const double length=ac_centers_float_cast(ac_directions_length(difference[0],difference[1],w),4);if(!std::isfinite(length)||length==0.0)return false;}
 nb::object normals=ac_call(plan.empty_like,{[&]{return path;}},{{"order",[&]{return plan.order;}}});owner.put("normals",normals);AccelerationCentersBuffer a;a.acquire(normals);if(!ac_directions_allocated2(a,n,w[0],plan.ndarray_type,normals))throw nb::type_error("native directions empty_like changed storage");
 nb::object lengths=ac_call(plan.empty,{[&]{return nb::int_(n);}},{{"dtype",[&]{return plan.float32_dtype;}}});owner.put("vec_len_arr",lengths);AccelerationCentersBuffer l;l.acquire(lengths);if(!ac_directions_allocated1(l,n,4,plan.ndarray_type,lengths))throw nb::type_error("native directions length allocation changed storage");
 for(Py_ssize_t i=start;i<n;++i){
  owner.put("i",nb::int_(i));const Py_ssize_t previous=i==0?n-1:i-1;
  nb::object difference=ac_call(plan.empty,{[]{return nb::int_(2);}},{{"dtype",[&]{return plan.difference_dtype;}}});AccelerationCentersBuffer d;d.acquire(difference);if(!ac_directions_allocated1(d,2,w[2],plan.ndarray_type,difference))throw nb::type_error("native directions difference allocation changed storage");
  for(Py_ssize_t k=0;k<2;++k){const double left=ac_directions_read2(p,i,k,w[0]),right=ac_directions_read2(p,previous,k,w[0]);d.write_float(k,0,ac_scalar_float_reduce(left,right,w[1],true),w[2]);}owner.put("vec_diff",difference);
  // Source squares, sum and sqrt precede the float32 destination store.
  const double first=d.read(0,w[2]),second=d.read(1,w[2]);const double length=ac_directions_length(first,second,w);l.write_float(previous,0,length,4);
  // Read the actual rounded float32 length back before normalization.
  for(Py_ssize_t k=0;k<2;++k){const double numerator=d.read(k,w[2]),denominator=l.read(previous,4);const double quotient=ac_centers_float_divide(numerator,denominator,w[6]);a.write_float(previous,k,ac_centers_float_cast(quotient,w[7]),w[0]);}
 }
 nb::object halves=ac_call(plan.empty,{[&]{return nb::int_(n);}},{{"dtype",[&]{return plan.half_dtype;}}});AccelerationCentersBuffer h;h.acquire(halves);if(!ac_directions_allocated1(h,n,w[9],plan.ndarray_type,halves))throw nb::type_error("native directions half allocation changed storage");
 // The open tail has unspecified source data. Never read it or initialize it.
 for(Py_ssize_t i=start;i<n;++i){const Py_ssize_t previous=i==0?n-1:i-1;h.write_float(previous,0,ac_centers_float_cast(ac_scalar_float_product(l.read(previous,4),0.5,w[8]),w[9]),w[9]);}
 result=ac_tuple({[&]{return normals;},[&]{return halves;}});return true;
}
// Triangle normalization owns the loop and uses the accepted native orientation.
constexpr const char* ac_normalize_triangles_plan_tag="shape_accel.normalize_triangles_float.v1";
struct AccelerationNormalizeTrianglesPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,point_dtype,empty;AccelerationOrientationFloatPlan orientation;
};
bool ac_normalize_triangles_plan_decode(nb::handle value,AccelerationNormalizeTrianglesPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_normalize_triangles_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=9)throw nb::type_error("native triangle normalization plan requires nine metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* point_dtype=PyTuple_GET_ITEM(value.ptr(),4);PyObject* empty=PyTuple_GET_ITEM(value.ptr(),5);PyObject* enum_type=PyTuple_GET_ITEM(value.ptr(),6);PyObject* members=PyTuple_GET_ITEM(value.ptr(),7);PyObject* val_constructor=PyTuple_GET_ITEM(value.ptr(),8);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=3||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=8||!PyType_Check(ndarray_type)||!PyCFunction_Check(empty)||!PyType_Check(enum_type)||!PyTuple_CheckExact(members)||PyTuple_GET_SIZE(members)!=3||!PyType_Check(val_constructor))throw nb::type_error("malformed native triangle normalization plan");
 for(Py_ssize_t i=0;i<3;++i)if(reinterpret_cast<PyObject*>(Py_TYPE(PyTuple_GET_ITEM(members,i)))!=enum_type)throw nb::type_error("triangle normalization requires genuine members of its enum type");
 for(Py_ssize_t i=0;i<8;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.point_dtype=nb::borrow<nb::object>(point_dtype);plan.empty=nb::borrow<nb::object>(empty);
 auto& o=plan.orientation;o.input1_bytes=o.input2_bytes=o.input3_bytes=plan.widths[4];o.sub21_bytes=o.sub31_bytes=plan.widths[5];o.product_bytes=plan.widths[6];o.reduction_bytes=plan.widths[7];o.ndarray_type=plan.ndarray_type;o.enum_type=nb::borrow<nb::object>(enum_type);o.clockwise=nb::borrow<nb::object>(PyTuple_GET_ITEM(members,0));o.anticlockwise=nb::borrow<nb::object>(PyTuple_GET_ITEM(members,1));o.collinear=nb::borrow<nb::object>(PyTuple_GET_ITEM(members,2));o.val_constructor=nb::borrow<nb::object>(val_constructor);return true;
}
int64_t ac_normalize_triangle_index(const AccelerationCentersBuffer& buffer,Py_ssize_t row,Py_ssize_t column,int width){if(width==4){int32_t value;std::memcpy(&value,buffer.address(row,column),4);return value;}int64_t value;std::memcpy(&value,buffer.address(row,column),8);return value;}
bool ac_normalize_point_index(int64_t index,Py_ssize_t count,Py_ssize_t& result){if(index<0){if(index<-static_cast<int64_t>(count))return false;result=static_cast<Py_ssize_t>(index+count);return true;}if(index>=count)return false;result=static_cast<Py_ssize_t>(index);return true;}
nb::object ac_normalize_point(const AccelerationNormalizeTrianglesPlan& plan,nb::object centers,nb::object offsets,nb::object ti,Py_ssize_t index){
 // The two genuine index loads and row views precede addition/allocation.
 nb::object center_index=ac_item(ti,nb::int_(index)),center_row=ac_item(centers,center_index);
 nb::object offset_index=ac_item(ti,nb::int_(index)),offset_row=ac_item(offsets,offset_index);
 AccelerationCentersBuffer c,o;c.acquire(center_row);o.acquire(offset_row);if(!c.input(plan.widths[1])||!o.input(plan.widths[2]))throw nb::type_error("native triangle normalization point views changed storage");
 nb::object point=ac_call(plan.empty,{[]{return nb::int_(2);}},{{"dtype",[&]{return plan.point_dtype;}}});AccelerationCentersBuffer p;p.acquire(point);if(!ac_directions_allocated1(p,2,plan.widths[4],plan.ndarray_type,point))throw nb::type_error("native triangle normalization point allocation changed storage");
 for(Py_ssize_t k=0;k<2;++k){const double left=c.read(k,plan.widths[1]),right=o.read(k,plan.widths[2]);p.write_float(k,0,ac_scalar_float_reduce(left,right,plan.widths[3],false),plan.widths[4]);}return point;
}
bool ac_normalize_triangles_fast(const AccelerationNormalizeTrianglesPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 const auto& w=plan.widths;nb::object triangles=owner.get("triangles"),centers=owner.get("centers"),offsets=owner.get("offsets");for(nb::handle value:{nb::handle(triangles),nb::handle(centers),nb::handle(offsets)})if(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))!=plan.ndarray_type.ptr())return false;
 AccelerationCentersBuffer t,c,o;t.acquire(triangles);c.acquire(centers);o.acquire(offsets);
 if(t.view.ndim!=2||!t.format(w[0],true)||t.view.readonly||t.view.shape[0]<0||t.view.shape[1]!=3||!(PyBuffer_IsContiguous(&t.view,'C')||PyBuffer_IsContiguous(&t.view,'F')))return false;
 if(c.view.ndim!=2||!c.format(w[1],false)||c.view.shape[0]<0||c.view.shape[1]!=2||o.view.ndim!=2||!o.format(w[2],false)||o.view.shape[0]<0||o.view.shape[1]!=2)return false;
 const Py_ssize_t n=t.view.shape[0];if(n==0){result=nb::none();return true;}
 uintptr_t t_first,t_last,c_first,c_last,o_first,o_last;if(!t.span(t_first,t_last)||!c.span(c_first,c_last)||!o.span(o_first,o_last))return false;
 if((t_first<c_last&&c_first<t_last)||(t_first<o_last&&o_first<t_last))return false;
 // Pure, source-ordered admission reads cannot observe an earlier native flip:
 // writable rows are disjoint and all target storage is disjoint from points.
 // Invalid/nonfinite graphs delegate before any caller locals or writes.
 for(Py_ssize_t i=0;i<n;++i)for(Py_ssize_t j=0;j<3;++j){Py_ssize_t ci,oi;const int64_t center_index=ac_normalize_triangle_index(t,i,j,w[0]);if(!ac_normalize_point_index(center_index,c.view.shape[0],ci))return false;const int64_t offset_index=ac_normalize_triangle_index(t,i,j,w[0]);if(!ac_normalize_point_index(offset_index,o.view.shape[0],oi))return false;for(Py_ssize_t k=0;k<2;++k){const double left=ac_directions_read2(c,ci,k,w[1]),right=ac_directions_read2(o,oi,k,w[2]);if(!std::isfinite(left)||!std::isfinite(right))return false;const double point=ac_centers_float_cast(ac_scalar_float_reduce(left,right,w[3],false),w[4]);if(!std::isfinite(point))return false;}}
 for(Py_ssize_t i=0;i<n;++i){
  owner.put("i",nb::int_(i));nb::object ti=ac_item(triangles,nb::int_(i));owner.put("ti",ti);AccelerationCentersBuffer row;row.acquire(ti);if(reinterpret_cast<PyObject*>(Py_TYPE(ti.ptr()))!=plan.ndarray_type.ptr()||row.view.ndim!=1||!row.format(w[0],true)||row.view.shape[0]!=3)throw nb::type_error("native triangle normalization row view changed storage");
  nb::object p1=ac_normalize_point(plan,centers,offsets,ti,0);owner.put("p1",p1);nb::object p2=ac_normalize_point(plan,centers,offsets,ti,1);owner.put("p2",p2);nb::object p3=ac_normalize_point(plan,centers,offsets,ti,2);owner.put("p3",p3);
  nb::object orientation;
  { // Original nested call locals; genuine parameter refs and assigned val.
   ShapeAccelerationLocals nested;nested.names={"p1","p2","p3","val"};nested.refs.resize(4);nested.cells.assign(4,false);nested.put("p1",p1);nested.put("p2",p2);nested.put("p3",p3);if(!ac_orientation_float_fast(plan.orientation,nested,orientation))throw nb::type_error("native triangle normalization orientation storage changed");
  }
  // Genuine enum singleton equality in the compiled original enum contract.
  if(orientation.ptr()==plan.orientation.clockwise.ptr()){
   const int64_t right0=ac_normalize_triangle_index(row,2,0,w[0]);const int64_t right1=ac_normalize_triangle_index(row,1,0,w[0]);const int64_t right2=ac_normalize_triangle_index(row,0,0,w[0]);
   t.write_integer(i,0,right0,w[0]);t.write_integer(i,1,right1,w[0]);t.write_integer(i,2,right2,w[0]);
  }
 }
 result=nb::none();return true;
}
// Native edge loop reuses the genuine accepted centers plan and source owner.
constexpr const char* ac_edge_loop_plan_tag="shape_accel.edge_loop_float.v1";
struct AccelerationEdgeLoopPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,orthogonal_dtype,half_dtype,empty;AccelerationCentersFloatPlan centers;
};
bool ac_edge_loop_plan_decode(nb::handle value,AccelerationEdgeLoopPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_edge_loop_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native edge loop plan requires seven metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* dtypes=PyTuple_GET_ITEM(value.ptr(),4);PyObject* empty=PyTuple_GET_ITEM(value.ptr(),5);PyObject* centers=PyTuple_GET_ITEM(value.ptr(),6);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=9||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=7||!PyType_Check(ndarray_type)||!PyTuple_CheckExact(dtypes)||PyTuple_GET_SIZE(dtypes)!=2||!PyCFunction_Check(empty))throw nb::type_error("malformed native edge loop plan");
 for(Py_ssize_t i=0;i<7;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));
 if(!ac_centers_float_plan_decode(nb::handle(centers),plan.centers))throw nb::type_error("native edge loop requires genuine centers-v1 metadata");
 const auto& w=plan.centers.widths;if(plan.centers.ndarray_type.ptr()!=ndarray_type||w[3]!=plan.widths[0]||w[4]!=plan.widths[1]||w[5]!=plan.widths[1]||w[6]!=plan.widths[2]||w[7]!=plan.widths[2])throw nb::value_error("native edge loop and centers precision metadata disagree");
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.orthogonal_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(dtypes,0));plan.half_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(dtypes,1));plan.empty=nb::borrow<nb::object>(empty);return true;
}
bool ac_edge_loop_output(const AccelerationCentersBuffer& buffer,int width,bool integer,int columns){return buffer.view.ndim==2&&buffer.format(width,integer)&&!buffer.view.readonly&&buffer.view.shape[0]>=0&&buffer.view.shape[1]==columns&&(PyBuffer_IsContiguous(&buffer.view,'C')||PyBuffer_IsContiguous(&buffer.view,'F'));}
nb::object ac_edge_loop_half_orthogonal(const AccelerationEdgeLoopPlan& plan,nb::object vector){
 AccelerationCentersBuffer v;v.acquire(vector);if(reinterpret_cast<PyObject*>(Py_TYPE(vector.ptr()))!=plan.ndarray_type.ptr()||!v.input(plan.widths[1]))throw nb::type_error("native edge loop direction view changed storage");
 // Original ccw=True array expression: v[1], then -v[0], then half multiply.
 const double first=v.read(1,plan.widths[1]);const double second=ac_centers_float_cast(-v.read(0,plan.widths[1]),plan.widths[3]);
 nb::object orthogonal=ac_call(plan.empty,{[]{return nb::int_(2);}},{{"dtype",[&]{return plan.orthogonal_dtype;}}});AccelerationCentersBuffer a;a.acquire(orthogonal);if(!ac_directions_allocated1(a,2,plan.widths[4],plan.ndarray_type,orthogonal))throw nb::type_error("native edge loop orthogonal allocation changed storage");a.write_float(0,0,first,plan.widths[4]);a.write_float(1,0,second,plan.widths[4]);
 nb::object half=ac_call(plan.empty,{[]{return nb::int_(2);}},{{"dtype",[&]{return plan.half_dtype;}}});AccelerationCentersBuffer h;h.acquire(half);if(!ac_directions_allocated1(h,2,plan.widths[6],plan.ndarray_type,half))throw nb::type_error("native edge loop half allocation changed storage");for(Py_ssize_t k=0;k<2;++k)h.write_float(k,0,ac_centers_float_cast(ac_scalar_float_product(0.5,a.read(k,plan.widths[4]),plan.widths[5]),plan.widths[6]),plan.widths[6]);return half;
}
void ac_edge_loop_store_row(const AccelerationCentersBuffer& target,Py_ssize_t index,int width,nb::object row,int source_width,nb::handle ndarray_type){
 AccelerationCentersBuffer source;source.acquire(row);if(reinterpret_cast<PyObject*>(Py_TYPE(row.ptr()))!=ndarray_type.ptr()||!source.input(source_width))throw nb::type_error("native edge loop row expression changed storage");const double first=source.read(0,source_width),second=source.read(1,source_width);target.write_float(index,0,first,width);target.write_float(index,1,second,width);
}
void ac_edge_loop_negate_stored(const AccelerationCentersBuffer& offsets,Py_ssize_t source,Py_ssize_t target,int width){const double first=ac_directions_read2(offsets,source,0,width),second=ac_directions_read2(offsets,source,1,width);offsets.write_float(target,0,-first,width);offsets.write_float(target,1,-second,width);}
bool ac_edge_loop_fast(const AccelerationEdgeLoopPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 const auto& w=plan.widths;const auto& cw=plan.centers.widths;
 nb::object path=owner.get("path"),directions=owner.get("direction_vectors"),limits=owner.get("bevel_limit_array"),centers=owner.get("centers"),offsets=owner.get("offsets"),triangles=owner.get("triangles");for(nb::handle value:{nb::handle(path),nb::handle(directions),nb::handle(limits),nb::handle(centers),nb::handle(offsets),nb::handle(triangles)})if(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))!=plan.ndarray_type.ptr())return false;
 nb::object closed_value=owner.get("closed"),bevel_value=owner.get("bevel"),cos_value=owner.get("cos_limit");if(!PyBool_Check(closed_value.ptr())||!PyBool_Check(bevel_value.ptr())||!PyFloat_CheckExact(cos_value.ptr()))return false;
 const bool closed=closed_value.ptr()==Py_True,always_bevel=bevel_value.ptr()==Py_True;const double cos_limit=ac_centers_float_cast(PyFloat_AS_DOUBLE(cos_value.ptr()),cw[8]);if(!std::isfinite(cos_limit))return false;
 AccelerationCentersBuffer p,d,l,c,o,t;p.acquire(path);d.acquire(directions);l.acquire(limits);c.acquire(centers);o.acquire(offsets);t.acquire(triangles);
 if(p.view.ndim!=2||!p.format(w[0],false)||p.view.shape[0]<2||p.view.shape[1]!=2||d.view.ndim!=2||!d.format(w[1],false)||d.view.shape[0]!=p.view.shape[0]||d.view.shape[1]!=2||l.view.ndim!=1||!l.format(w[2],false)||l.view.shape[0]!=p.view.shape[0])return false;
 if(!ac_edge_loop_output(c,cw[0],false,2)||!ac_edge_loop_output(o,cw[1],false,2)||!ac_edge_loop_output(t,cw[2],true,3))return false;
 const AccelerationCentersBuffer* storage[]={&c,&o,&t,&p,&d,&l};uintptr_t first[6],last[6];for(size_t i=0;i<6;++i)if(!storage[i]->span(first[i],last[i]))return false;for(size_t i=0;i<3;++i)for(size_t k=i+1;k<6;++k)if(first[i]<last[k]&&first[k]<last[i])return false;
 const Py_ssize_t n=d.view.shape[0],start=closed?0:1,stop=closed?n:n-1;
 // Check only source-used rows. Open last direction/length slots stay unread.
 for(Py_ssize_t i=0;i<p.view.shape[0];++i)for(Py_ssize_t k=0;k<2;++k)if(!std::isfinite(ac_directions_read2(p,i,k,w[0])))return false;
 for(Py_ssize_t i=0;i<(closed?n:n-1);++i)for(Py_ssize_t k=0;k<2;++k)if(!std::isfinite(ac_directions_read2(d,i,k,w[1])))return false;
 Py_ssize_t checked_j=closed?0:2;if(!closed&&(c.view.shape[0]<2||o.view.shape[0]<2||t.view.shape[0]<2))return false;
 if(start<stop&&(c.view.shape[0]<3||o.view.shape[0]<3||t.view.shape[0]<3))return false; // accepted child buffer admission
 for(Py_ssize_t i=start;i<stop;++i){const Py_ssize_t previous=i==0?n-1:i-1;if(!std::isfinite(l.read(previous,w[2]))||!std::isfinite(l.read(i,w[2])))return false;const double first_left=ac_directions_read2(d,previous,0,w[1]),first_right=ac_directions_read2(d,i,0,w[1]);const double first_product=ac_scalar_float_product(first_left,first_right,cw[9]);const double second_left=ac_directions_read2(d,previous,1,w[1]),second_right=ac_directions_read2(d,i,1,w[1]);const double second_product=ac_scalar_float_product(second_left,second_right,cw[9]);const double cosine=ac_scalar_float_reduce(first_product,second_product,cw[10],false);const Py_ssize_t count=2+static_cast<Py_ssize_t>(always_bevel||cosine<cos_limit);if(checked_j>PY_SSIZE_T_MAX-4||checked_j>c.view.shape[0]-count||checked_j>o.view.shape[0]-count||checked_j>t.view.shape[0]-count)return false;checked_j+=count;}
 if(checked_j>PY_SSIZE_T_MAX-2||checked_j>c.view.shape[0]-2||checked_j>o.view.shape[0]-2)return false;
 // Every admitted child guard is established before source j or any writes.
 Py_ssize_t j=0;owner.put("j",nb::int_(j));
 if(!closed){
  nb::object vertex=ac_item(path,nb::int_(0));ac_edge_loop_store_row(c,0,cw[0],vertex,w[0],plan.ndarray_type);ac_edge_loop_store_row(c,1,cw[0],vertex,w[0],plan.ndarray_type);vertex.reset();
  nb::object half=ac_edge_loop_half_orthogonal(plan,ac_item(directions,nb::int_(0)));ac_edge_loop_store_row(o,0,cw[1],half,w[6],plan.ndarray_type);half.reset();ac_edge_loop_negate_stored(o,0,1,cw[1]);
  t.write_integer(0,0,0,cw[2]);t.write_integer(0,1,1,cw[2]);t.write_integer(0,2,2,cw[2]);t.write_integer(1,0,1,cw[2]);t.write_integer(1,1,2,cw[2]);t.write_integer(1,2,3,cw[2]);j=2;owner.put("j",nb::int_(j));
 }
 for(Py_ssize_t i=start;i<stop;++i){
  owner.put("i",nb::int_(i));nb::object added;
  { // Capture original arguments left-to-right; real row/scalar transport refs.
   ShapeAccelerationLocals child;child.names={"centers","offsets","triangles","vertex","vec1","vec2","half_vec1_len","half_vec2_len","j","cos_limit","always_bevel","cos_angle","sin_angle","bevel","i","miter","scale_factor","sign","mag","clockwise","counterclockwise","invert"};child.refs.resize(child.names.size());child.cells.assign(child.names.size(),false);child.put("centers",centers);child.put("offsets",offsets);child.put("triangles",triangles);child.put("vertex",ac_item(path,nb::int_(i)));child.put("vec1",ac_item(directions,nb::int_(i-1)));child.put("vec2",ac_item(directions,nb::int_(i)));
   nb::object half1=ac_item(limits,nb::int_(i-1));double half1_value=PyFloat_AsDouble(half1.ptr());if(PyErr_Occurred())throw nb::python_error();child.put("half_vec1_len",nb::float_(half1_value));half1.reset();nb::object half2=ac_item(limits,nb::int_(i));double half2_value=PyFloat_AsDouble(half2.ptr());if(PyErr_Occurred())throw nb::python_error();child.put("half_vec2_len",nb::float_(half2_value));half2.reset();child.put("j",nb::int_(j));child.put("cos_limit",cos_value);child.put("always_bevel",bevel_value);
   if(!ac_centers_float_fast(plan.centers,child,added))throw nb::type_error("native edge loop admitted centers storage changed");
  }
  const Py_ssize_t increment=PyLong_AsSsize_t(added.ptr());if(PyErr_Occurred())throw nb::python_error();j+=increment;owner.put("j",nb::int_(j));
 }
 if(closed){
  ac_edge_loop_store_row(c,j,cw[0],ac_item(centers,nb::int_(0)),cw[0],plan.ndarray_type);ac_edge_loop_store_row(c,j+1,cw[0],ac_item(centers,nb::int_(1)),cw[0],plan.ndarray_type);ac_edge_loop_store_row(o,j,cw[1],ac_item(offsets,nb::int_(0)),cw[1],plan.ndarray_type);ac_edge_loop_store_row(o,j+1,cw[1],ac_item(offsets,nb::int_(1)),cw[1],plan.ndarray_type);
 }else{
  ac_edge_loop_store_row(c,j,cw[0],ac_item(path,nb::int_(-1)),w[0],plan.ndarray_type);ac_edge_loop_store_row(c,j+1,cw[0],ac_item(path,nb::int_(-1)),w[0],plan.ndarray_type);nb::object half=ac_edge_loop_half_orthogonal(plan,ac_item(directions,nb::int_(-2)));ac_edge_loop_store_row(o,j,cw[1],half,w[6],plan.ndarray_type);half.reset();ac_edge_loop_negate_stored(o,j,j+1,cw[1]);
 }
 result=nb::none();return true;
}
// Polar and output-size plans retain genuine immutable compiler metadata.
constexpr const char* ac_polar_angles_plan_tag="shape_accel.polar_angles_float.v1";
struct AccelerationPolarAnglesPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,pi,start_constructor,angle_constructor,merged_constructor,previous_constructor,atan2;
};
bool ac_polar_angles_plan_decode(nb::handle value,AccelerationPolarAnglesPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_polar_angles_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native polar-angle plan requires seven metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* pi=PyTuple_GET_ITEM(value.ptr(),4);PyObject* constructors=PyTuple_GET_ITEM(value.ptr(),5);PyObject* atan2=PyTuple_GET_ITEM(value.ptr(),6);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=2||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=10||!PyType_Check(ndarray_type)||!PyFloat_CheckExact(pi)||!PyTuple_CheckExact(constructors)||PyTuple_GET_SIZE(constructors)!=4||!PyCFunction_Check(atan2))throw nb::type_error("malformed native polar-angle plan");
 for(Py_ssize_t i=0;i<10;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));for(Py_ssize_t i=0;i<4;++i)if(!PyType_Check(PyTuple_GET_ITEM(constructors,i)))throw nb::type_error("polar-angle plan requires genuine scalar constructors");
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.pi=nb::borrow<nb::object>(pi);plan.start_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,0));plan.angle_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,1));plan.merged_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,2));plan.previous_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(constructors,3));plan.atan2=nb::borrow<nb::object>(atan2);return true;
}
double ac_polar_angles_atan2(double y,double x,int argument_width,int result_width){if(argument_width==4){const float fy=static_cast<float>(y),fx=static_cast<float>(x);volatile float value=std::atan2(fy,fx);return ac_centers_float_cast(value,result_width);}volatile double value=std::atan2(y,x);return ac_centers_float_cast(value,result_width);}
double ac_polar_angles_position(const AccelerationPolarAnglesPlan& plan,const AccelerationCentersBuffer& vertices,const AccelerationCentersBuffer& centroid,Py_ssize_t index){
 const auto& w=plan.widths;const double y_left=ac_directions_read2(vertices,index,1,w[0]),y_right=centroid.read(1,w[1]);const double y=ac_scalar_float_reduce(y_left,y_right,w[2],true);const double x_left=ac_directions_read2(vertices,index,0,w[0]),x_right=centroid.read(0,w[1]);const double x=ac_scalar_float_reduce(x_left,x_right,w[2],true);return ac_polar_angles_atan2(y,x,w[3],w[4]);
}
bool ac_polar_angles_finite_position(const AccelerationPolarAnglesPlan& plan,const AccelerationCentersBuffer& vertices,const AccelerationCentersBuffer& centroid,Py_ssize_t index,double& angle){
 const auto& w=plan.widths;const double y_left=ac_directions_read2(vertices,index,1,w[0]),y_right=centroid.read(1,w[1]);if(!std::isfinite(y_left)||!std::isfinite(y_right))return false;const double y=ac_scalar_float_reduce(y_left,y_right,w[2],true);if(!std::isfinite(y))return false;const double x_left=ac_directions_read2(vertices,index,0,w[0]),x_right=centroid.read(0,w[1]);if(!std::isfinite(x_left)||!std::isfinite(x_right))return false;const double x=ac_scalar_float_reduce(x_left,x_right,w[2],true);if(!std::isfinite(x))return false;angle=ac_polar_angles_atan2(y,x,w[3],w[4]);return std::isfinite(angle);
}
bool ac_polar_angles_addressable(const AccelerationCentersBuffer& buffer){
 uintptr_t first,last;if(!buffer.span(first,last))return false;uintptr_t extent=0;
 for(int i=0;i<buffer.view.ndim;++i){const uintptr_t count=static_cast<uintptr_t>(buffer.view.shape[i]-1);const Py_ssize_t stride=buffer.view.strides[i];const uintptr_t step=stride<0?static_cast<uintptr_t>(-(stride+1))+1:static_cast<uintptr_t>(stride);if(count&&step>static_cast<uintptr_t>(PY_SSIZE_T_MAX)/count)return false;const uintptr_t delta=count*step;if(delta>static_cast<uintptr_t>(PY_SSIZE_T_MAX)-extent)return false;extent+=delta;}return true;
}
bool ac_polar_angles_fast(const AccelerationPolarAnglesPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 const auto& w=plan.widths;nb::object vertices=owner.get("vertices"),centroid=owner.get("centroid");if(reinterpret_cast<PyObject*>(Py_TYPE(vertices.ptr()))!=plan.ndarray_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(centroid.ptr()))!=plan.ndarray_type.ptr())return false;
 AccelerationCentersBuffer v,c;v.acquire(vertices);c.acquire(centroid);if(v.view.ndim!=2||!v.format(w[0],false)||v.view.shape[0]<1||v.view.shape[1]!=2||!c.input(w[1])||c.view.shape[0]!=2)return false;if(!ac_polar_angles_addressable(v)||!ac_polar_angles_addressable(c))return false;
 const double pi=PyFloat_AS_DOUBLE(plan.pi.ptr());if(!std::isfinite(pi))return false;const double two_pi=ac_scalar_float_product(2.0,pi,w[6]);if(!std::isfinite(two_pi))return false;
 // Preflight follows the same early-return path. It never scans a skipped tail.
 double checked_start;if(!ac_polar_angles_finite_position(plan,v,c,0,checked_start))return false;double checked_previous=0.0;
 for(Py_ssize_t i=1;i<v.view.shape[0];++i){double current;if(!ac_polar_angles_finite_position(plan,v,c,i,current))return false;double angle=ac_scalar_float_reduce(current,checked_start,w[5],true);if(!std::isfinite(angle))return false;if(angle<0.0)angle=ac_scalar_float_reduce(angle,two_pi,w[7],false);angle=ac_centers_float_cast(angle,w[8]);if(!std::isfinite(angle))return false;if(angle<checked_previous)break;checked_previous=ac_centers_float_cast(angle,w[9]);}
 const double start_angle=ac_polar_angles_position(plan,v,c,0);owner.put("start_angle",ac_call(plan.start_constructor,{[&]{return nb::float_(start_angle);}}));double previous=0.0;owner.put("prev_angle",nb::float_(0.0));
 for(Py_ssize_t i=1;i<v.view.shape[0];++i){owner.put("i",nb::int_(i));const double current=ac_polar_angles_position(plan,v,c,i);double angle=ac_scalar_float_reduce(current,start_angle,w[5],true);owner.put("angle",ac_call(plan.angle_constructor,{[&]{return nb::float_(angle);}}));if(angle<0.0)angle=ac_scalar_float_reduce(angle,two_pi,w[7],false);angle=ac_centers_float_cast(angle,w[8]);owner.put("angle",ac_call(plan.merged_constructor,{[&]{return nb::float_(angle);}}));if(angle<previous){result=nb::bool_(false);return true;}previous=ac_centers_float_cast(angle,w[9]);owner.put("prev_angle",ac_call(plan.previous_constructor,{[&]{return nb::float_(previous);}}));}
 result=nb::bool_(true);return true;
}
constexpr const char* ac_output_size_plan_tag="shape_accel.output_size_float.v1";
struct AccelerationOutputSizePlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,cos_constructor;AccelerationScalarFloatPlan dot;
};
bool ac_output_size_plan_decode(nb::handle value,AccelerationOutputSizePlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_output_size_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=6)throw nb::type_error("native output-size plan requires six metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* dot=PyTuple_GET_ITEM(value.ptr(),4);PyObject* cos_constructor=PyTuple_GET_ITEM(value.ptr(),5);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=4||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=4||!PyType_Check(ndarray_type)||!PyType_Check(cos_constructor))throw nb::type_error("malformed native output-size plan");for(Py_ssize_t i=0;i<4;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));if(!ac_scalar_float_plan_decode(nb::handle(dot),plan.dot)||plan.dot.ndarray_type.ptr()!=ndarray_type||plan.dot.left_bytes!=plan.widths[0]||plan.dot.right_bytes!=plan.widths[0])throw nb::type_error("output-size plan requires matching genuine scalar-v1 metadata");plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.cos_constructor=nb::borrow<nb::object>(cos_constructor);return true;
}
bool ac_output_size_fast(const AccelerationOutputSizePlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object directions=owner.get("direction_vectors"),closed_value=owner.get("closed"),bevel_value=owner.get("bevel");if(reinterpret_cast<PyObject*>(Py_TYPE(directions.ptr()))!=plan.ndarray_type.ptr()||!PyBool_Check(closed_value.ptr())||!PyBool_Check(bevel_value.ptr()))return false;
 AccelerationCentersBuffer d;d.acquire(directions);if(d.view.ndim!=2||!d.format(plan.widths[0],false)||d.view.shape[0]<0)return false;const Py_ssize_t n=d.view.shape[0];if(n>(INT64_MAX-2)/3)return false;const bool closed=closed_value.ptr()==Py_True,bevel=bevel_value.ptr()==Py_True;const int64_t point_count=2*static_cast<int64_t>(n)+2*static_cast<int64_t>(closed);const Py_ssize_t start=closed?0:1,stop=closed?n:n-1;
 nb::object limit;double cos_limit=0.0;if(!bevel&&start<stop){if(d.view.shape[1]!=2)return false;limit=owner.get("cos_miter_limit");if(!PyFloat_CheckExact(limit.ptr()))return false;cos_limit=ac_centers_float_cast(PyFloat_AS_DOUBLE(limit.ptr()),plan.widths[1]);}
 // Early bevel/empty-loop paths deliberately read no vector or limit values.
 owner.put("n",nb::int_(n));owner.put("point_count",nb::int_(point_count));if(bevel){const int64_t bevel_count=static_cast<int64_t>(n)-2*static_cast<int64_t>(!closed);owner.put("bevel_count",nb::int_(bevel_count));result=nb::int_(point_count+bevel_count);return true;}
 int64_t bevel_count=0;owner.put("bevel_count",nb::int_(0));owner.put("start",nb::int_(start));owner.put("stop",nb::int_(stop));
 for(Py_ssize_t i=start;i<stop;++i){owner.put("i",nb::int_(i));nb::object cosine;
  {ShapeAccelerationLocals child;child.names={"v0","v1"};child.refs.resize(2);child.cells.assign(2,false);child.put("v0",ac_item(directions,nb::int_(i-1)));child.put("v1",ac_item(directions,nb::int_(i)));if(!ac_scalar_float_fast("_dot",plan.dot,child,cosine))throw nb::type_error("native output-size admitted dot storage changed");}
  const double value=PyFloat_AsDouble(cosine.ptr());if(PyErr_Occurred())throw nb::python_error();owner.put("cos_angle",ac_call(plan.cos_constructor,{[&]{return nb::float_(value);}}));const double left=ac_centers_float_cast(value,plan.widths[2]),right=ac_centers_float_cast(cos_limit,plan.widths[3]);if(left<right){++bevel_count;owner.put("bevel_count",nb::int_(bevel_count));}
 }
 result=nb::int_(point_count+bevel_count);return true;
}
// Bounding-box scalar arithmetic and stores are owned by compiled C++.
constexpr const char* ac_bounding_box_plan_tag="shape_accel.bounding_box_float.v1";
struct AccelerationBoundingBoxPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,scalar_constructor,float32_dtype,empty;
};
bool ac_bounding_box_plan_decode(nb::handle value,AccelerationBoundingBoxPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;
 int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_bounding_box_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=6)throw nb::type_error("native bounding-box plan requires six metadata fields");
 PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* declarations=PyTuple_GET_ITEM(value.ptr(),4);PyObject* empty=PyTuple_GET_ITEM(value.ptr(),5);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=1||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=9||!PyType_Check(ndarray_type)||!PyTuple_CheckExact(declarations)||PyTuple_GET_SIZE(declarations)!=2||!PyType_Check(PyTuple_GET_ITEM(declarations,0))||!PyType_Check(PyTuple_GET_ITEM(declarations,1))||!PyCFunction_Check(empty))throw nb::type_error("malformed native bounding-box plan");
 for(Py_ssize_t i=0;i<9;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));const auto& w=plan.widths;if(w[0]!=w[1]||w[2]!=w[3]||w[3]!=w[4]||w[5]!=w[6]||w[6]!=w[7]||w[8]!=4)throw nb::type_error("unsupported native bounding-box precision");
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.scalar_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,0));plan.float32_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,1));plan.empty=nb::borrow<nb::object>(empty);return true;
}
bool ac_bounding_box_consumed_addresses(const AccelerationCentersBuffer& buffer){
 // Only the source first two rows/columns participate in this address guard.
 uintptr_t low=0,high=0,extent=0;for(int i=0;i<2;++i){const Py_ssize_t stride=buffer.view.strides[i];const uintptr_t step=stride<0?static_cast<uintptr_t>(-(stride+1))+1:static_cast<uintptr_t>(stride);if(step>static_cast<uintptr_t>(PY_SSIZE_T_MAX)-extent)return false;extent+=step;uintptr_t& end=stride<0?low:high;if(step>UINTPTR_MAX-end)return false;end+=step;}
 const uintptr_t pointer=reinterpret_cast<uintptr_t>(buffer.view.buf);return pointer>=low&&high<=UINTPTR_MAX-pointer&&static_cast<uintptr_t>(buffer.view.itemsize)<=UINTPTR_MAX-(pointer+high);
}
double ac_bounding_box_midpoint(double left,double right,const std::vector<int>& widths){
 const double a=ac_centers_float_cast(left,widths[2]),b=ac_centers_float_cast(right,widths[3]);const double sum=ac_scalar_float_reduce(a,b,widths[4],false);const double numerator=ac_centers_float_cast(sum,widths[5]),denominator=ac_centers_float_cast(2.0,widths[6]);return ac_centers_float_divide(numerator,denominator,widths[7]);
}
// Exact IEEE float32 maximum; range admission precedes every float32 cast.
bool ac_bounding_box_storable(double value){return std::isfinite(value)&&std::fabs(value)<=0x1.fffffep127;}
bool ac_bounding_box_fast(const AccelerationBoundingBoxPlan& plan,ShapeAccelerationLocals& owner,nb::object& returned){
 const auto& w=plan.widths;nb::object bounding_box=owner.get("bounding_box");if(reinterpret_cast<PyObject*>(Py_TYPE(bounding_box.ptr()))!=plan.ndarray_type.ptr())return false;
 AccelerationCentersBuffer input;input.acquire(bounding_box);if(input.view.ndim!=2||!input.format(w[0],false)||input.view.shape[0]<2||input.view.shape[1]<2||!ac_bounding_box_consumed_addresses(input))return false;
 // Pure finite admission reads exactly the four source-selected coordinates.
 const double checked_x_min=ac_directions_read2(input,0,0,w[0]),checked_x_max=ac_directions_read2(input,1,0,w[0]),checked_y_min=ac_directions_read2(input,0,1,w[0]),checked_y_max=ac_directions_read2(input,1,1,w[0]);
 for(double value:{checked_x_min,checked_x_max,checked_y_min,checked_y_max})if(!ac_bounding_box_storable(value))return false;
 const double checked_x_mid=ac_bounding_box_midpoint(checked_x_min,checked_x_max,w),checked_y_mid=ac_bounding_box_midpoint(checked_y_min,checked_y_max,w);if(!ac_bounding_box_storable(checked_x_mid)||!ac_bounding_box_storable(checked_y_mid))return false;
 const double x_min=ac_centers_float_cast(ac_directions_read2(input,0,0,w[0]),w[1]);owner.put("x_min",ac_call(plan.scalar_constructor,{[&]{return nb::float_(x_min);}}));
 const double x_max=ac_centers_float_cast(ac_directions_read2(input,1,0,w[0]),w[1]);owner.put("x_max",ac_call(plan.scalar_constructor,{[&]{return nb::float_(x_max);}}));
 const double y_min=ac_centers_float_cast(ac_directions_read2(input,0,1,w[0]),w[1]);owner.put("y_min",ac_call(plan.scalar_constructor,{[&]{return nb::float_(y_min);}}));
 const double y_max=ac_centers_float_cast(ac_directions_read2(input,1,1,w[0]),w[1]);owner.put("y_max",ac_call(plan.scalar_constructor,{[&]{return nb::float_(y_max);}}));
 nb::object result=ac_call(plan.empty,{[]{return ac_tuple({[]{return nb::int_(9);},[]{return nb::int_(2);}});}},{{"dtype",[&]{return plan.float32_dtype;}}});owner.put("result",result);
 AccelerationCentersBuffer output;output.acquire(result);if(!ac_directions_allocated2(output,9,4,plan.ndarray_type,result)||!PyBuffer_IsContiguous(&output.view,'C'))throw nb::type_error("native bounding-box empty allocation changed storage");
 // Each original list RHS is evaluated left to right before its row stores.
 {const double left=x_min;const double right=y_min;output.write_float(0,0,left,4);output.write_float(0,1,right,4);}
 {const double left=ac_bounding_box_midpoint(x_min,x_max,w);const double right=y_min;output.write_float(1,0,left,4);output.write_float(1,1,right,4);}
 {const double left=x_max;const double right=y_min;output.write_float(2,0,left,4);output.write_float(2,1,right,4);}
 {const double left=x_max;const double right=ac_bounding_box_midpoint(y_min,y_max,w);output.write_float(3,0,left,4);output.write_float(3,1,right,4);}
 {const double left=x_max;const double right=y_max;output.write_float(4,0,left,4);output.write_float(4,1,right,4);}
 {const double left=ac_bounding_box_midpoint(x_min,x_max,w);const double right=y_max;output.write_float(5,0,left,4);output.write_float(5,1,right,4);}
 {const double left=x_min;const double right=y_max;output.write_float(6,0,left,4);output.write_float(6,1,right,4);}
 {const double left=x_min;const double right=ac_bounding_box_midpoint(y_min,y_max,w);output.write_float(7,0,left,4);output.write_float(7,1,right,4);}
 {const double left=ac_bounding_box_midpoint(x_min,x_max,w);const double right=ac_bounding_box_midpoint(y_min,y_max,w);output.write_float(8,0,left,4);output.write_float(8,1,right,4);}
 returned=result;return true;
}
// Native convex and edge owners retain their actual source fields and children.
nb::object shape_accel_numba_call(nb::object,nb::tuple,nb::dict,ShapeAccelerationLocals&,nb::object);
ShapeAccelerationLocals ac_parent_source_owner(std::initializer_list<const char*> names){ShapeAccelerationLocals owner;for(const char* name:names)owner.names.emplace_back(name);owner.refs.resize(owner.names.size());owner.cells.assign(owner.names.size(),false);return owner;}
nb::object ac_parent_child_fallback(const char* operation,nb::object metadata,nb::dict globals,ShapeAccelerationLocals& child){return shape_accel_numba_call(nb::str(operation),nb::tuple(),std::move(globals),child,std::move(metadata));}
constexpr const char* ac_convex_plan_tag="shape_accel.convex_float.v1";
struct AccelerationConvexPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,float32_dtype,empty,mean_constructor,iteration;AccelerationOrientationFloatPlan orientation;AccelerationPolarAnglesPlan forward,reverse;
};
bool ac_convex_plan_decode(nb::handle value,AccelerationConvexPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_convex_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=9)throw nb::type_error("native convex plan requires nine metadata fields");PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* declarations=PyTuple_GET_ITEM(value.ptr(),4);PyObject* iteration=PyTuple_GET_ITEM(value.ptr(),5);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=1||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=10||!PyType_Check(ndarray_type)||!PyTuple_CheckExact(declarations)||PyTuple_GET_SIZE(declarations)!=3||!PyType_Check(PyTuple_GET_ITEM(declarations,0))||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,1))||!PyType_Check(PyTuple_GET_ITEM(declarations,2))||!PyTuple_CheckExact(iteration)||PyTuple_GET_SIZE(iteration)!=3)throw nb::type_error("malformed native convex plan");
 for(Py_ssize_t i=0;i<10;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));const auto& w=plan.widths;if(w[0]!=w[1]||w[1]!=w[8]||w[2]!=w[3]||w[3]!=w[4]||w[5]!=w[6]||w[6]!=w[7]||w[9]!=4)throw nb::type_error("unsupported native convex mean precision");
 if(!ac_orientation_float_plan_decode(nb::handle(PyTuple_GET_ITEM(value.ptr(),6)),plan.orientation)||!ac_polar_angles_plan_decode(nb::handle(PyTuple_GET_ITEM(value.ptr(),7)),plan.forward)||!ac_polar_angles_plan_decode(nb::handle(PyTuple_GET_ITEM(value.ptr(),8)),plan.reverse))throw nb::type_error("convex plan requires genuine accepted child metadata");if(plan.orientation.ndarray_type.ptr()!=ndarray_type||plan.forward.ndarray_type.ptr()!=ndarray_type||plan.reverse.ndarray_type.ptr()!=ndarray_type||plan.orientation.input1_bytes!=w[0]||plan.orientation.input2_bytes!=w[0]||plan.orientation.input3_bytes!=w[0]||plan.forward.widths[0]!=w[0]||plan.reverse.widths[0]!=w[0]||plan.forward.widths[1]!=4||plan.reverse.widths[1]!=4)throw nb::type_error("convex child precision or declaration mismatch");
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.float32_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,0));plan.empty=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,1));plan.mean_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,2));plan.iteration=nb::borrow<nb::object>(iteration);return true;
}
int ac_convex_orientation_value(const AccelerationConvexPlan& plan,const AccelerationCentersBuffer& input,Py_ssize_t p1,Py_ssize_t p2,Py_ssize_t p3,bool& valid){
 const auto& op=plan.orientation;auto read=[&](Py_ssize_t row,Py_ssize_t column){const double value=ac_directions_read2(input,row,column,plan.widths[0]);if(!std::isfinite(value))valid=false;return value;};
 const double first21_left=read(p2,0),first21_right=read(p1,0);const double first21=ac_scalar_float_reduce(first21_left,first21_right,op.sub21_bytes,true);const double first31_left=read(p3,1),first31_right=read(p1,1);const double first31=ac_scalar_float_reduce(first31_left,first31_right,op.sub31_bytes,true);const double first=ac_scalar_float_product(first21,first31,op.product_bytes);
 const double second21_left=read(p2,1),second21_right=read(p1,1);const double second21=ac_scalar_float_reduce(second21_left,second21_right,op.sub21_bytes,true);const double second31_left=read(p3,0),second31_right=read(p1,0);const double second31=ac_scalar_float_reduce(second31_left,second31_right,op.sub31_bytes,true);const double second=ac_scalar_float_product(second21,second31,op.product_bytes);const double val=ac_scalar_float_reduce(first,second,op.reduction_bytes,true);if(!std::isfinite(val))valid=false;return val<0.0?-1:val>0.0?1:0;
}
double ac_convex_mean(const AccelerationConvexPlan& plan,const AccelerationCentersBuffer& input,Py_ssize_t column,bool& valid){
 const auto& w=plan.widths;double accumulator=ac_centers_float_cast(0.0,w[1]);for(Py_ssize_t i=0;i<input.view.shape[0];++i){const double value=ac_directions_read2(input,i,column,w[0]);if(!std::isfinite(value))valid=false;const double left=ac_centers_float_cast(accumulator,w[2]),right=ac_centers_float_cast(value,w[3]);accumulator=ac_scalar_float_reduce(left,right,w[4],false);if(!std::isfinite(accumulator))valid=false;}
 const double numerator=ac_centers_float_cast(accumulator,w[5]),denominator=ac_centers_float_cast(static_cast<double>(input.view.shape[0]),w[6]);const double divided=ac_centers_float_divide(numerator,denominator,w[7]);const double result=ac_centers_float_cast(divided,w[8]);if(!std::isfinite(result))valid=false;return result;
}
bool ac_convex_polar_preflight(const AccelerationPolarAnglesPlan& plan,const AccelerationCentersBuffer& input,double center0,double center1,bool reversed){
 const auto& w=plan.widths;const Py_ssize_t n=input.view.shape[0];auto position=[&](Py_ssize_t i,double& result){const Py_ssize_t row=reversed?n-1-i:i;const double y_left=ac_directions_read2(input,row,1,w[0]),y=ac_scalar_float_reduce(y_left,center1,w[2],true);const double x_left=ac_directions_read2(input,row,0,w[0]),x=ac_scalar_float_reduce(x_left,center0,w[2],true);if(!std::isfinite(y_left)||!std::isfinite(y)||!std::isfinite(x_left)||!std::isfinite(x))return false;result=ac_polar_angles_atan2(y,x,w[3],w[4]);return std::isfinite(result);};
 const double pi=PyFloat_AS_DOUBLE(plan.pi.ptr()),two_pi=ac_scalar_float_product(2.0,pi,w[6]);if(!std::isfinite(pi)||!std::isfinite(two_pi))return false;double start;if(!position(0,start))return false;double previous=0.0;for(Py_ssize_t i=1;i<n;++i){double current;if(!position(i,current))return false;double angle=ac_scalar_float_reduce(current,start,w[5],true);if(!std::isfinite(angle))return false;if(angle<0.0)angle=ac_scalar_float_reduce(angle,two_pi,w[7],false);angle=ac_centers_float_cast(angle,w[8]);if(!std::isfinite(angle))return false;if(angle<previous)break;previous=ac_centers_float_cast(angle,w[9]);}return true;
}
bool ac_convex_preflight(const AccelerationConvexPlan& plan,const AccelerationCentersBuffer& input){
 const Py_ssize_t n=input.view.shape[0];int orientation=0;bool orientation_set=false,valid=true;for(Py_ssize_t i=0;i<n-2;++i){const int current=ac_convex_orientation_value(plan,input,i,i+1,i+2,valid);if(!valid)return false;if(current){if(!orientation_set){orientation=current;orientation_set=true;}else if(current!=orientation)return true;}}
 if(!orientation)return true;for(int pass=0;pass<2;++pass){const Py_ssize_t a=pass?n-1:n-2,b=pass?0:n-1,c=pass?1:0;const int current=ac_convex_orientation_value(plan,input,a,b,c,valid);if(!valid)return false;if(current&&current!=orientation)return true;}
 const double mean0=ac_convex_mean(plan,input,0,valid);if(!valid||!ac_bounding_box_storable(mean0))return false;const double center0=ac_centers_float_cast(mean0,4);const double mean1=ac_convex_mean(plan,input,1,valid);if(!valid||!ac_bounding_box_storable(mean1))return false;const double center1=ac_centers_float_cast(mean1,4);return ac_convex_polar_preflight(orientation==1?plan.forward:plan.reverse,input,center0,center1,orientation!=1);
}
nb::object ac_convex_orientation_child(const AccelerationConvexPlan& plan,nb::object p1,nb::object p2,nb::object p3){
 auto child=ac_parent_source_owner({"p1","p2","p3","val"});child.put("p1",std::move(p1));child.put("p2",std::move(p2));child.put("p3",std::move(p3));nb::object result;if(!ac_orientation_float_fast(plan.orientation,child,result))throw nb::type_error("native convex admitted orientation storage changed");return result;
}
bool ac_convex_fast(const AccelerationConvexPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object vertices=owner.get("vertices");if(reinterpret_cast<PyObject*>(Py_TYPE(vertices.ptr()))!=plan.ndarray_type.ptr())return false;AccelerationCentersBuffer input;input.acquire(vertices);if(input.view.ndim!=2||!input.format(plan.widths[0],false)||input.view.shape[0]<0)return false;const Py_ssize_t n=input.view.shape[0];if(n<3){result=nb::bool_(false);return true;}if(n==3){result=nb::bool_(true);return true;}if(input.view.shape[1]!=2||!ac_polar_angles_addressable(input)||!ac_convex_preflight(plan,input))return false;
 nb::object orientation=plan.orientation.collinear;owner.put("orientation_",orientation);owner.put("idx",nb::int_(0));owner.put("n_points",nb::int_(n));bool orientation_set=false;owner.put("orientation_set",nb::bool_(false));
 for(Py_ssize_t idx=0;idx<n-2;++idx){owner.put("idx",nb::int_(idx));nb::object p1=ac_item(vertices,nb::int_(idx));owner.put("p1",p1);nb::object p2=ac_item(vertices,nb::int_(idx+1));owner.put("p2",p2);nb::object p3=ac_item(vertices,nb::int_(idx+2));owner.put("p3",p3);nb::object current=ac_convex_orientation_child(plan,p1,p2,p3);owner.put("current_orientation",current);if(current.ptr()!=plan.orientation.collinear.ptr()){if(!orientation_set){orientation=current;owner.put("orientation_",orientation);orientation_set=true;owner.put("orientation_set",nb::bool_(true));}else if(current.ptr()!=orientation.ptr()){result=nb::bool_(false);return true;}}}
 if(orientation.ptr()==plan.orientation.collinear.ptr()){result=nb::bool_(false);return true;}
 for(int pass=0;pass<2;++pass){const Py_ssize_t idx0=pass?n-1:n-2,idx1=pass?0:n-1,idx2=pass?1:0;owner.put("idx0",nb::int_(idx0));owner.put("idx1",nb::int_(idx1));owner.put("idx2",nb::int_(idx2));nb::object triangle;{nb::object p1=ac_item(vertices,nb::int_(idx0));nb::object p2=ac_item(vertices,nb::int_(idx1));nb::object p3=ac_item(vertices,nb::int_(idx2));triangle=ac_convex_orientation_child(plan,p1,p2,p3);}owner.put("triangle_orientation",triangle);if(triangle.ptr()!=plan.orientation.collinear.ptr()&&triangle.ptr()!=orientation.ptr()){result=nb::bool_(false);return true;}}
 nb::object centroid=ac_call(plan.empty,{[]{return nb::int_(2);}},{{"dtype",[&]{return plan.float32_dtype;}}});owner.put("centroid",centroid);AccelerationCentersBuffer c;c.acquire(centroid);if(!ac_directions_allocated1(c,2,4,plan.ndarray_type,centroid))throw nb::type_error("native convex centroid allocation changed storage");bool valid=true;const double mean0=ac_convex_mean(plan,input,0,valid);c.write_float(0,0,mean0,4);const double mean1=ac_convex_mean(plan,input,1,valid);c.write_float(1,0,mean1,4);
 auto child=ac_parent_source_owner({"vertices","centroid","start_angle","prev_angle","i","angle"});nb::object polar_vertices=vertices;const bool forward=orientation.ptr()==plan.orientation.anticlockwise.ptr();if(!forward)polar_vertices=ac_item(vertices,ac_slice(nb::none(),nb::none(),nb::int_(-1)));child.put("vertices",std::move(polar_vertices));child.put("centroid",centroid);if(!ac_polar_angles_fast(forward?plan.forward:plan.reverse,child,result))throw nb::type_error("native convex admitted polar storage changed");return true;
}
constexpr const char* ac_edge_owner_plan_tag="shape_accel.edge_owner_float.v1";
struct AccelerationEdgeOwnerPlan{
 std::vector<int> widths;nb::object arguments,ndarray_type,asarray,empty,zeros,float32_dtype,int32_dtype,cos_constructor,conversion_type,directions_metadata,size_metadata,loop_metadata,normalize_metadata;AccelerationDirectionsFloatPlan directions;AccelerationOutputSizePlan size;AccelerationEdgeLoopPlan loop;AccelerationNormalizeTrianglesPlan normalize;
};
bool ac_edge_owner_plan_decode(nb::handle value,AccelerationEdgeOwnerPlan& plan){
 if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),ac_edge_owner_plan_tag);if(PyErr_Occurred())throw nb::python_error();if(equal!=0)return false;
 if(PyTuple_GET_SIZE(value.ptr())!=8)throw nb::type_error("native edge-owner plan requires eight metadata fields");PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1);PyObject* widths=PyTuple_GET_ITEM(value.ptr(),2);PyObject* ndarray_type=PyTuple_GET_ITEM(value.ptr(),3);PyObject* declarations=PyTuple_GET_ITEM(value.ptr(),4);PyObject* cos_constructor=PyTuple_GET_ITEM(value.ptr(),5);PyObject* children=PyTuple_GET_ITEM(value.ptr(),6);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=4||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=16||!PyType_Check(ndarray_type)||!PyTuple_CheckExact(declarations)||PyTuple_GET_SIZE(declarations)!=5||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,0))||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,1))||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,2))||!PyType_Check(PyTuple_GET_ITEM(declarations,3))||!PyType_Check(PyTuple_GET_ITEM(declarations,4))||!PyType_Check(cos_constructor)||!PyTuple_CheckExact(children)||PyTuple_GET_SIZE(children)!=4)throw nb::type_error("malformed native edge-owner plan");
 for(Py_ssize_t i=0;i<16;++i)plan.widths.push_back(ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,i))));if(!ac_directions_float_plan_decode(nb::handle(PyTuple_GET_ITEM(children,0)),plan.directions)||!ac_output_size_plan_decode(nb::handle(PyTuple_GET_ITEM(children,1)),plan.size)||!ac_edge_loop_plan_decode(nb::handle(PyTuple_GET_ITEM(children,2)),plan.loop)||!ac_normalize_triangles_plan_decode(nb::handle(PyTuple_GET_ITEM(children,3)),plan.normalize))throw nb::type_error("edge-owner plan requires genuine accepted child metadata");
 if(plan.directions.ndarray_type.ptr()!=ndarray_type||plan.size.ndarray_type.ptr()!=ndarray_type||plan.loop.ndarray_type.ptr()!=ndarray_type||plan.normalize.ndarray_type.ptr()!=ndarray_type||plan.directions.widths[0]!=4||plan.size.widths[0]!=4||plan.loop.widths[0]!=4||plan.loop.widths[1]!=4||plan.normalize.widths[0]!=4||plan.normalize.widths[1]!=4||plan.normalize.widths[2]!=4)throw nb::type_error("edge-owner child declarations or precision mismatch");
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(ndarray_type);plan.asarray=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,0));plan.empty=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,1));plan.zeros=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,2));plan.float32_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,3));plan.int32_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,4));plan.cos_constructor=nb::borrow<nb::object>(cos_constructor);plan.conversion_type=nb::borrow<nb::object>(PyTuple_GET_ITEM(value.ptr(),7));plan.directions_metadata=nb::borrow<nb::object>(PyTuple_GET_ITEM(children,0));plan.size_metadata=nb::borrow<nb::object>(PyTuple_GET_ITEM(children,1));plan.loop_metadata=nb::borrow<nb::object>(PyTuple_GET_ITEM(children,2));plan.normalize_metadata=nb::borrow<nb::object>(PyTuple_GET_ITEM(children,3));return true;
}
double ac_edge_owner_cosine(const AccelerationEdgeOwnerPlan& plan,double limit){
 const auto& w=plan.widths;const double left=ac_centers_float_cast(ac_centers_float_cast(limit,w[1]),w[2]),right=ac_centers_float_cast(2.0,w[3]);const double half=ac_centers_float_divide(left,right,w[4]);const double base=ac_centers_float_cast(half,w[5]);const double square=ac_scalar_float_product(base,base,w[6]);const double denominator=ac_scalar_float_product(ac_centers_float_cast(2.0,w[7]),ac_centers_float_cast(square,w[8]),w[9]);const double reciprocal=ac_centers_float_divide(ac_centers_float_cast(1.0,w[10]),ac_centers_float_cast(denominator,w[11]),w[12]);return ac_scalar_float_reduce(ac_centers_float_cast(reciprocal,w[13]),ac_centers_float_cast(1.0,w[14]),w[15],true);
}
nb::object ac_edge_owner_allocate2(const AccelerationEdgeOwnerPlan& plan,Py_ssize_t rows,int columns,bool zero,bool integer){
 return ac_call(zero?plan.zeros:plan.empty,{[&]{return ac_tuple({[&]{return nb::int_(rows);},[&]{return nb::int_(columns);}});}},{{"dtype",[&]{return integer?plan.int32_dtype:plan.float32_dtype;}}});
}
nb::object ac_edge_owner_trim(const AccelerationEdgeOwnerPlan& plan,nb::object path){
 auto child=ac_parent_source_owner({"path","path_"});child.put("path",path);nb::object converted=ac_call(plan.asarray,{[&]{return path;}},{{"dtype",[&]{return plan.float32_dtype;}}});child.put("path_",converted);AccelerationCentersBuffer p;p.acquire(converted);if(reinterpret_cast<PyObject*>(Py_TYPE(converted.ptr()))!=plan.ndarray_type.ptr()||p.view.ndim!=2||!p.format(4,false)||p.view.shape[0]<2||p.view.shape[1]!=2||!ac_polar_angles_addressable(p))throw nb::type_error("native edge-owner asarray changed admitted storage");
 const Py_ssize_t n=p.view.shape[0];const double last0=ac_directions_read2(p,n-1,0,4),previous0=ac_directions_read2(p,n-2,0,4),last1=ac_directions_read2(p,n-1,1,4),previous1=ac_directions_read2(p,n-2,1,4);if(last0==previous0&&last1==previous1)return ac_item(converted,ac_slice(nb::none(),nb::int_(-1)));return converted;
}
bool ac_edge_owner_fast(const AccelerationEdgeOwnerPlan& plan,nb::dict globals,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object original=owner.get("path"),closed=owner.get("closed"),bevel=owner.get("bevel");if(reinterpret_cast<PyObject*>(Py_TYPE(original.ptr()))!=plan.ndarray_type.ptr()||!PyBool_Check(closed.ptr())||!PyBool_Check(bevel.ptr()))return false;AccelerationCentersBuffer source;source.acquire(original);if(source.view.ndim!=2||!source.format(plan.widths[0],false)||source.view.shape[0]<2||source.view.shape[1]!=2||!ac_polar_angles_addressable(source))return false;const Py_ssize_t original_n=source.view.shape[0];if(original_n>(INT32_MAX-2)/3)return false;
 const double raw_last0=ac_directions_read2(source,original_n-1,0,plan.widths[0]),raw_previous0=ac_directions_read2(source,original_n-2,0,plan.widths[0]),raw_last1=ac_directions_read2(source,original_n-1,1,plan.widths[0]),raw_previous1=ac_directions_read2(source,original_n-2,1,plan.widths[0]);for(double value:{raw_last0,raw_previous0,raw_last1,raw_previous1})if(!ac_bounding_box_storable(value))return false;const double last0=ac_centers_float_cast(raw_last0,4),previous0=ac_centers_float_cast(raw_previous0,4),last1=ac_centers_float_cast(raw_last1,4),previous1=ac_centers_float_cast(raw_previous1,4);const Py_ssize_t checked_n=original_n-static_cast<Py_ssize_t>(last0==previous0&&last1==previous1);
 nb::object limit;double limit_value=0.0;if(checked_n>=2){limit=owner.get("limit");if(!PyFloat_CheckExact(limit.ptr()))return false;limit_value=ac_centers_float_cast(PyFloat_AS_DOUBLE(limit.ptr()),plan.widths[1]);if(!std::isfinite(limit_value))return false;const auto& w=plan.widths;const double half=ac_centers_float_divide(ac_centers_float_cast(limit_value,w[2]),ac_centers_float_cast(2.0,w[3]),w[4]);const double base=ac_centers_float_cast(half,w[5]),square=ac_scalar_float_product(base,base,w[6]),denominator=ac_scalar_float_product(ac_centers_float_cast(2.0,w[7]),ac_centers_float_cast(square,w[8]),w[9]);if(!std::isfinite(half)||!std::isfinite(square)||!std::isfinite(denominator)||denominator==0.0||!std::isfinite(ac_edge_owner_cosine(plan,limit_value)))return false;}
 for(Py_ssize_t i=0;i<original_n;++i)for(Py_ssize_t k=0;k<2;++k)if(!ac_bounding_box_storable(ac_directions_read2(source,i,k,plan.widths[0])))return false;
 // The source trim call executes first. No whole-parent generic restart follows.
 nb::object path=ac_edge_owner_trim(plan,original);owner.put("path",path);AccelerationCentersBuffer p;p.acquire(path);const Py_ssize_t n=p.view.shape[0];
 if(n<2){nb::object centers=ac_edge_owner_allocate2(plan,4,2,false,false);owner.put("centers",centers);AccelerationCentersBuffer c;c.acquire(centers);if(!ac_directions_allocated2(c,4,4,plan.ndarray_type,centers)||!PyBuffer_IsContiguous(&c.view,'C'))throw nb::type_error("native short edge centers allocation changed storage");for(Py_ssize_t i=0;i<4;++i){const double first=ac_directions_read2(p,0,0,4),second=ac_directions_read2(p,0,1,4);c.write_float(i,0,first,4);c.write_float(i,1,second,4);}
  nb::object triangles=ac_edge_owner_allocate2(plan,2,3,false,true);owner.put("triangles",triangles);AccelerationCentersBuffer t;t.acquire(triangles);if(!ac_edge_loop_output(t,4,true,3)||t.view.shape[0]!=2||!PyBuffer_IsContiguous(&t.view,'C'))throw nb::type_error("native short edge triangle allocation changed storage");t.write_integer(0,0,0,4);t.write_integer(0,1,1,4);t.write_integer(0,2,3,4);t.write_integer(1,0,1,4);t.write_integer(1,1,3,4);t.write_integer(1,2,2,4);nb::object zero=ac_edge_owner_allocate2(plan,4,2,true,false);result=ac_tuple({[&]{return centers;},[&]{return zero;},[&]{return triangles;}});return true;}
 const double cos_limit=ac_edge_owner_cosine(plan,limit_value);nb::object cos_value=ac_call(plan.cos_constructor,{[&]{return nb::float_(cos_limit);}});owner.put("cos_limit",cos_value);nb::object cos_argument=nb::float_(cos_limit);
 nb::object direction_result;{auto child=ac_parent_source_owner({"path","closed","normals","vec_len_arr","i","vec_diff"});child.put("path",path);child.put("closed",closed);if(!ac_directions_float_fast(plan.directions,child,direction_result))direction_result=ac_parent_child_fallback("_direction_vec_and_half_length",plan.directions_metadata,globals,child);}
 auto direction=ac_unpack(std::move(direction_result),2);nb::object direction_vectors=std::move(direction[0]);owner.put("direction_vectors",direction_vectors);nb::object bevel_limit_array=std::move(direction[1]);owner.put("bevel_limit_array",bevel_limit_array);
 nb::object point_count;{auto child=ac_parent_source_owner({"direction_vectors","closed","cos_miter_limit","bevel","n","point_count","bevel_count","start","stop","i","cos_angle"});child.put("direction_vectors",direction_vectors);child.put("closed",closed);child.put("cos_miter_limit",cos_argument);child.put("bevel",bevel);if(!ac_output_size_fast(plan.size,child,point_count))point_count=ac_parent_child_fallback("_calc_output_size",plan.size_metadata,globals,child);}owner.put("point_count",point_count);const Py_ssize_t count=PyLong_AsSsize_t(point_count.ptr());if(PyErr_Occurred())throw nb::python_error();
 nb::object centers=ac_edge_owner_allocate2(plan,count,2,false,false);owner.put("centers",centers);nb::object offsets=ac_edge_owner_allocate2(plan,count,2,false,false);owner.put("offsets",offsets);nb::object triangles=ac_edge_owner_allocate2(plan,count-2,3,false,true);owner.put("triangles",triangles);
 {auto child=ac_parent_source_owner({"path","closed","cos_limit","bevel","direction_vectors","bevel_limit_array","centers","offsets","triangles","j","i"});child.put("path",path);child.put("closed",closed);child.put("cos_limit",cos_argument);child.put("bevel",bevel);child.put("direction_vectors",direction_vectors);child.put("bevel_limit_array",bevel_limit_array);child.put("centers",centers);child.put("offsets",offsets);child.put("triangles",triangles);nb::object ignored;if(!ac_edge_loop_fast(plan.loop,child,ignored))ignored=ac_parent_child_fallback("_generate_2D_edge_meshes_loop",plan.loop_metadata,globals,child);}
 {auto child=ac_parent_source_owner({"triangles","centers","offsets","i","ti","p1","p2","p3"});child.put("triangles",triangles);child.put("centers",centers);child.put("offsets",offsets);nb::object ignored;if(!ac_normalize_triangles_fast(plan.normalize,child,ignored))ignored=ac_parent_child_fallback("_normalize_triangle_orientation",plan.normalize_metadata,globals,child);}
 result=ac_tuple({[&]{return centers;},[&]{return offsets;},[&]{return triangles;}});return true;
}
// Independent immutable plans for finite floating normalize/dedup owners.
constexpr const char* ac_dedup_plan_tag="shape_accel.dedup_float.v1";
constexpr const char* ac_normalize_vertices_plan_tag="shape_accel.normalize_vertices_float.v1";
struct AccelerationDedupPlan{int bytes=0;nb::object arguments,ndarray_type,empty,dtype,result_type;bool writable=true;};
struct AccelerationNormalizeVerticesPlan{int bytes=0;nb::object arguments,ndarray_type,empty,array,float32_dtype,int64_dtype,source_types;};
bool ac_owner_pair_tag(nb::handle value,const char* tag){if(!PyTuple_CheckExact(value.ptr())||PyTuple_GET_SIZE(value.ptr())==0||!PyUnicode_CheckExact(PyTuple_GET_ITEM(value.ptr(),0)))return false;const int equal=PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(value.ptr(),0),tag);if(PyErr_Occurred())throw nb::python_error();return equal==0;}
bool ac_dedup_plan_decode(nb::handle value,AccelerationDedupPlan& plan){
 if(!ac_owner_pair_tag(value,ac_dedup_plan_tag))return false;if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native dedup plan requires seven metadata fields");PyObject* args=PyTuple_GET_ITEM(value.ptr(),1),*width=PyTuple_GET_ITEM(value.ptr(),2),*ndarray=PyTuple_GET_ITEM(value.ptr(),3),*empty=PyTuple_GET_ITEM(value.ptr(),4),*dtype=PyTuple_GET_ITEM(value.ptr(),5),*result=PyTuple_GET_ITEM(value.ptr(),6);if(!PyTuple_CheckExact(args)||PyTuple_GET_SIZE(args)!=2||!PyType_Check(ndarray)||!PyCFunction_Check(empty))throw nb::type_error("malformed native dedup plan");plan.bytes=ac_scalar_plan_width(nb::handle(width));plan.arguments=nb::borrow<nb::object>(args);plan.ndarray_type=nb::borrow<nb::object>(ndarray);plan.empty=nb::borrow<nb::object>(empty);plan.dtype=nb::borrow<nb::object>(dtype);plan.result_type=nb::borrow<nb::object>(result);plan.writable=ac_truth(ac_attr(plan.result_type,"mutable"));return true;
}
bool ac_normalize_vertices_plan_decode(nb::handle value,AccelerationNormalizeVerticesPlan& plan){
 if(!ac_owner_pair_tag(value,ac_normalize_vertices_plan_tag))return false;if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native vertex normalization plan requires seven metadata fields");PyObject* args=PyTuple_GET_ITEM(value.ptr(),1),*widths=PyTuple_GET_ITEM(value.ptr(),2),*ndarray=PyTuple_GET_ITEM(value.ptr(),3),*declarations=PyTuple_GET_ITEM(value.ptr(),4),*source_types=PyTuple_GET_ITEM(value.ptr(),5),*intp=PyTuple_GET_ITEM(value.ptr(),6);if(!PyTuple_CheckExact(args)||PyTuple_GET_SIZE(args)!=2||!PyTuple_CheckExact(widths)||PyTuple_GET_SIZE(widths)!=3||!PyType_Check(ndarray)||!PyTuple_CheckExact(declarations)||PyTuple_GET_SIZE(declarations)!=4||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,0))||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,1))||!PyType_Check(PyTuple_GET_ITEM(declarations,2))||!PyType_Check(PyTuple_GET_ITEM(declarations,3))||!PyTuple_CheckExact(source_types)||PyTuple_GET_SIZE(source_types)!=3||PyLong_AsLong(intp)!=64)throw nb::type_error("malformed native vertex normalization plan");if(PyErr_Occurred())throw nb::python_error();plan.bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(widths,0)));if(PyLong_AsLong(PyTuple_GET_ITEM(widths,1))!=4||PyLong_AsLong(PyTuple_GET_ITEM(widths,2))!=8)throw nb::type_error("native normalization output precision changed");if(PyErr_Occurred())throw nb::python_error();plan.arguments=nb::borrow<nb::object>(args);plan.ndarray_type=nb::borrow<nb::object>(ndarray);plan.empty=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,0));plan.array=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,1));plan.float32_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,2));plan.int64_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,3));plan.source_types=nb::borrow<nb::object>(source_types);return true;
}
bool ac_dedup_row_equal(const AccelerationCentersBuffer& left,Py_ssize_t lr,const AccelerationCentersBuffer& right,Py_ssize_t rr,int bytes){bool equal=true;for(Py_ssize_t column=0;column<left.view.shape[1];++column){const double a=ac_directions_read2(left,lr,column,bytes),b=ac_directions_read2(right,rr,column,bytes);if(a!=b)equal=false;}return equal;}
void ac_dedup_copy_row(const AccelerationCentersBuffer& output,Py_ssize_t row,const AccelerationCentersBuffer& input,Py_ssize_t source,int bytes){for(Py_ssize_t column=0;column<input.view.shape[1];++column)std::memcpy(output.address(row,column),input.address(source,column),static_cast<size_t>(bytes));}
bool ac_dedup_fast(const AccelerationDedupPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object path=owner.get("path"),closed=owner.get("closed");if(reinterpret_cast<PyObject*>(Py_TYPE(path.ptr()))!=plan.ndarray_type.ptr()||!PyBool_Check(closed.ptr()))return false;AccelerationCentersBuffer input;input.acquire(path);if(input.view.ndim!=2||!input.format(plan.bytes,false)||!ac_polar_angles_addressable(input))return false;const Py_ssize_t n=input.view.shape[0],columns=input.view.shape[1];if(n<=2){result=path;return true;}if(columns<1||columns>PY_SSIZE_T_MAX/plan.bytes||n>PY_SSIZE_T_MAX/(columns*plan.bytes))return false;for(Py_ssize_t row=0;row<n;++row)for(Py_ssize_t column=0;column<columns;++column)if(!std::isfinite(ac_directions_read2(input,row,column,plan.bytes)))return false;
 Py_ssize_t count=0;owner.put("dup_count",nb::int_(0));for(Py_ssize_t i=0;i<n-2;++i){owner.put("i",nb::int_(i));if(ac_dedup_row_equal(input,i,input,i+1,plan.bytes)){++count;owner.put("dup_count",nb::int_(count));}}if(closed.ptr()==Py_True&&ac_dedup_row_equal(input,0,input,n-1,plan.bytes)){++count;owner.put("dup_count",nb::int_(count));}if(count==0){result=path;return true;}const Py_ssize_t target=n-count;owner.put("target_len",nb::int_(target));nb::object new_path=ac_call(plan.empty,{[&]{return T(E(nb::int_(target)),E(nb::int_(columns)));}},{{"dtype",[&]{return plan.dtype;}}});owner.put("new_path",new_path);AccelerationCentersBuffer output;output.acquire(new_path);if(output.view.ndim!=2||!output.format(plan.bytes,false)||output.view.readonly||output.view.shape[0]!=target||output.view.shape[1]!=columns||!ac_polar_angles_addressable(output))throw nb::type_error("native dedup allocation changed source storage");ac_dedup_copy_row(output,0,input,0,plan.bytes);Py_ssize_t index=0;owner.put("index",nb::int_(0));for(Py_ssize_t i=1;i<n;++i){owner.put("i",nb::int_(i));if(index==target-1)break;if(!ac_dedup_row_equal(output,index,input,i,plan.bytes)){ac_dedup_copy_row(output,index+1,input,i,plan.bytes);++index;owner.put("index",nb::int_(index));}}if(!plan.writable){nb::object flags=ac_attr(new_path,"flags");if(PyObject_SetAttrString(flags.ptr(),"writeable",Py_False)<0)throw nb::python_error();}result=new_path;return true;
}
bool ac_normalize_vertices_fast(const AccelerationNormalizeVerticesPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object vertices=owner.get("vertices"),close=owner.get("close");if(reinterpret_cast<PyObject*>(Py_TYPE(vertices.ptr()))!=plan.ndarray_type.ptr()||!PyBool_Check(close.ptr()))return false;AccelerationCentersBuffer input;input.acquire(vertices);if(input.view.ndim!=2||input.view.shape[1]!=2||input.view.shape[0]<2||!input.format(plan.bytes,false)||!ac_polar_angles_addressable(input))return false;const Py_ssize_t n=input.view.shape[0];if(n>PY_SSIZE_T_MAX/16)return false;for(Py_ssize_t row=0;row<n;++row)for(Py_ssize_t column=0;column<2;++column)if(!ac_bounding_box_storable(ac_directions_read2(input,row,column,plan.bytes)))return false;
 // Preserve short-circuit endpoint read order and the actual source slice.
 const double first_x=ac_directions_read2(input,0,0,plan.bytes),last_x=ac_directions_read2(input,n-1,0,plan.bytes);bool trim=false;if(first_x==last_x){const double first_y=ac_directions_read2(input,0,1,plan.bytes),last_y=ac_directions_read2(input,n-1,1,plan.bytes);trim=first_y==last_y;}if(trim){vertices=ac_item(vertices,ac_slice(nb::none(),nb::int_(-1)));owner.put("vertices",vertices);close=nb::bool_(true);owner.put("close",close);}
 nb::dict dictionary;owner.put("vertex_to_idx",dictionary);nb::list new_vertices;owner.put("new_vertices",new_vertices);nb::object edges=ac_check(PySet_New(nullptr));owner.put("edges",edges);int64_t previous=0,next=0;owner.put("prev_idx",nb::int_(0));owner.put("i",nb::int_(0));nb::object iterator=ac_iter(vertices);while(nb::object vertex=ac_next(iterator)){owner.put("vertex",vertex);nb::object vertex_t=T(E(J(vertex,ZERO)),E(J(vertex,ONE)));owner.put("vertex_t",vertex_t);nb::object key=ac_normalize_typed_key(vertex_t,true);PyObject* existing=PyDict_GetItemWithError(dictionary.ptr(),key.ptr());if(!existing&&PyErr_Occurred())throw nb::python_error();int64_t current;if(existing){current=PyLong_AsLongLong(existing);if(PyErr_Occurred())throw nb::python_error();owner.put("current_idx",nb::borrow<nb::object>(existing));}else{current=next;nb::object index=nb::int_(next);owner.put("current_idx",index);if(PyDict_SetItem(dictionary.ptr(),key.ptr(),index.ptr())<0)throw nb::python_error();new_vertices.append(vertex);++next;owner.put("i",nb::int_(next));}nb::object edge=previous<current?T(E(nb::int_(previous)),E(nb::int_(current))):T(E(nb::int_(current)),E(nb::int_(previous)));owner.put("edge",edge);const int exists=PySet_Contains(edges.ptr(),edge.ptr());if(exists<0)throw nb::python_error();if(exists){if(PySet_Discard(edges.ptr(),edge.ptr())<0)throw nb::python_error();}else if(PySet_Add(edges.ptr(),edge.ptr())<0)throw nb::python_error();previous=current;owner.put("prev_idx",nb::int_(current));}iterator.reset();
 if(close.ptr()==Py_True){nb::object vertex_t=T(E(J(vertices,T(E(nb::int_(-1)),E(ZERO)))),E(J(vertices,T(E(nb::int_(-1)),E(ONE)))));owner.put("vertex_t",vertex_t);nb::object index=ac_item(dictionary,ac_normalize_typed_key(vertex_t,true));owner.put("idx",index);nb::object edge=T(E(ZERO),E(index));owner.put("edge",edge);const int exists=PySet_Contains(edges.ptr(),edge.ptr());if(exists<0)throw nb::python_error();if(exists){if(PySet_Discard(edges.ptr(),edge.ptr())<0)throw nb::python_error();}else if(PySet_Add(edges.ptr(),edge.ptr())<0)throw nb::python_error();}
 nb::object self=T(E(ZERO),E(ZERO));const int removed=PySet_Discard(edges.ptr(),self.ptr());if(removed<0)throw nb::python_error();if(removed==0){PyErr_SetString(PyExc_KeyError,"set.remove(): key not in set");throw nb::python_error();}const Py_ssize_t unique=PyList_GET_SIZE(new_vertices.ptr());nb::object array=ac_call(plan.empty,{[&]{return T(E(nb::int_(unique)),E(TWO));}},{{"dtype",[&]{return plan.float32_dtype;}}});owner.put("new_vertices_array",array);AccelerationCentersBuffer output;output.acquire(array);if(output.view.ndim!=2||output.view.shape[0]!=unique||output.view.shape[1]!=2||output.view.readonly||!output.format(4,false)||!ac_polar_angles_addressable(output))throw nb::type_error("native normalization allocation changed source storage");for(Py_ssize_t i=0;i<unique;++i){owner.put("i",nb::int_(i));nb::object vertex=nb::borrow<nb::object>(PyList_GET_ITEM(new_vertices.ptr(),i));owner.put("vertex",vertex);AccelerationCentersBuffer row;row.acquire(vertex);if(!row.input(plan.bytes))throw nb::type_error("native normalization source row storage changed");for(Py_ssize_t column=0;column<2;++column){volatile float value=static_cast<float>(row.read(column,plan.bytes));const float stored=value;std::memcpy(output.address(i,column),&stored,4);}}
 nb::object list=ac_normalize_ordered_edges(vertices,close,dictionary,edges,true),edge_array=ac_normalize_edges_array(plan.array,list,plan.int64_dtype,true);owner.put("edges_array",edge_array);result=T(E(array),E(edge_array));return true;
}

// Reconstruction uses genuine public typed-list primitives, not private NRT
// offsets or a second graph. Canonical incident/visited/poly/polygons own state.
constexpr const char* ac_reconstruct_plan_tag="shape_accel.reconstruct.v1";
struct AccelerationReconstructPlan{
 int vertex_bytes=0,edge_bytes=0;bool vertex_integer=false;nb::object arguments,ndarray_type,list_type,zeros,bool_dtype,array,edge_constructor,source_types,empty_list,append,getitem,iterate;
};
bool ac_reconstruct_plan_decode(nb::handle value,AccelerationReconstructPlan& plan){
 if(!ac_owner_pair_tag(value,ac_reconstruct_plan_tag))return false;if(PyTuple_GET_SIZE(value.ptr())!=7)throw nb::type_error("native reconstruction plan requires seven metadata fields");PyObject* arguments=PyTuple_GET_ITEM(value.ptr(),1),*storage=PyTuple_GET_ITEM(value.ptr(),2),*classes=PyTuple_GET_ITEM(value.ptr(),3),*declarations=PyTuple_GET_ITEM(value.ptr(),4),*source_types=PyTuple_GET_ITEM(value.ptr(),5),*primitives=PyTuple_GET_ITEM(value.ptr(),6);
 if(!PyTuple_CheckExact(arguments)||PyTuple_GET_SIZE(arguments)!=2||!PyTuple_CheckExact(storage)||PyTuple_GET_SIZE(storage)!=4||!PyBool_Check(PyTuple_GET_ITEM(storage,0))||!PyTuple_CheckExact(classes)||PyTuple_GET_SIZE(classes)!=2||!PyType_Check(PyTuple_GET_ITEM(classes,0))||!PyType_Check(PyTuple_GET_ITEM(classes,1))||!PyTuple_CheckExact(declarations)||PyTuple_GET_SIZE(declarations)!=4||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,0))||!PyType_Check(PyTuple_GET_ITEM(declarations,1))||!PyCFunction_Check(PyTuple_GET_ITEM(declarations,2))||!PyType_Check(PyTuple_GET_ITEM(declarations,3))||!PyTuple_CheckExact(source_types)||PyTuple_GET_SIZE(source_types)!=7||!PyTuple_CheckExact(primitives)||PyTuple_GET_SIZE(primitives)!=4||!PyMethod_Check(PyTuple_GET_ITEM(primitives,0))||!PyFunction_Check(PyTuple_GET_ITEM(primitives,1))||!PyFunction_Check(PyTuple_GET_ITEM(primitives,2))||!PyFunction_Check(PyTuple_GET_ITEM(primitives,3)))throw nb::type_error("malformed native reconstruction plan");
 plan.vertex_integer=PyTuple_GET_ITEM(storage,0)==Py_True;plan.vertex_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(storage,1)));plan.edge_bytes=ac_scalar_plan_width(nb::handle(PyTuple_GET_ITEM(storage,2)));if(PyLong_AsLong(PyTuple_GET_ITEM(storage,3))!=64)throw nb::type_error("native reconstruction requires actual 64-bit source counters");if(PyErr_Occurred())throw nb::python_error();
 plan.arguments=nb::borrow<nb::object>(arguments);plan.ndarray_type=nb::borrow<nb::object>(PyTuple_GET_ITEM(classes,0));plan.list_type=nb::borrow<nb::object>(PyTuple_GET_ITEM(classes,1));plan.zeros=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,0));plan.bool_dtype=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,1));plan.array=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,2));plan.edge_constructor=nb::borrow<nb::object>(PyTuple_GET_ITEM(declarations,3));plan.source_types=nb::borrow<nb::object>(source_types);plan.empty_list=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,0));plan.append=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,1));plan.getitem=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,2));plan.iterate=nb::borrow<nb::object>(PyTuple_GET_ITEM(primitives,3));return true;
}
bool ac_reconstruct_primitive_metadata_current(const AccelerationReconstructPlan& plan){
 for(const auto& primitive:std::initializer_list<std::pair<const char*,nb::object>>{{"append",plan.append},{"__getitem__",plan.getitem},{"__iter__",plan.iterate}}){if(ac_attr(plan.list_type,primitive.first).ptr()!=primitive.second.ptr())return false;}nb::object empty=ac_attr(plan.list_type,"empty_list");return PyMethod_Check(empty.ptr())&&PyMethod_GET_FUNCTION(empty.ptr())==PyMethod_GET_FUNCTION(plan.empty_list.ptr())&&PyMethod_GET_SELF(empty.ptr())==PyMethod_GET_SELF(plan.empty_list.ptr());
}
nb::object ac_reconstruct_typed_empty(const AccelerationReconstructPlan& plan,nb::object list_type){
 nb::object item_type=ac_attr(list_type,"item_type"),value=ac_call(plan.empty_list,{E(item_type)});if(reinterpret_cast<PyObject*>(Py_TYPE(value.ptr()))!=plan.list_type.ptr())throw nb::type_error("native reconstruction primitive changed list class");const int equal=PyObject_RichCompareBool(ac_attr(value,"_numba_type_").ptr(),list_type.ptr(),Py_EQ);if(equal<0)throw nb::python_error();if(!equal)throw nb::type_error("native reconstruction primitive changed actual list element type");return value;
}
int64_t ac_reconstruct_edge_value(const AccelerationCentersBuffer& edges,Py_ssize_t row,Py_ssize_t column,int bytes){if(bytes==4){int32_t value;std::memcpy(&value,edges.address(row,column),4);return value;}int64_t value;std::memcpy(&value,edges.address(row,column),8);return value;}
nb::object ac_reconstruct_edge_scalar(const AccelerationReconstructPlan& plan,int64_t value){return ac_call(plan.edge_constructor,{E(nb::int_(value))});}
nb::object ac_reconstruct_list_item(const AccelerationReconstructPlan& plan,nb::object list,nb::object index){return ac_call(plan.getitem,{E(list),E(index)});}
void ac_reconstruct_list_append(const AccelerationReconstructPlan& plan,nb::object list,nb::object value){ac_call(plan.append,{E(list),E(value)});}
bool ac_reconstruct_fast(const AccelerationReconstructPlan& plan,ShapeAccelerationLocals& owner,nb::object& result){
 nb::object vertices=owner.get("vertices"),edges=owner.get("edges");if(reinterpret_cast<PyObject*>(Py_TYPE(vertices.ptr()))!=plan.ndarray_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(edges.ptr()))!=plan.ndarray_type.ptr()||!ac_reconstruct_primitive_metadata_current(plan))return false;AccelerationCentersBuffer v,e;v.acquire(vertices);e.acquire(edges);if(v.view.ndim!=2||(v.view.shape[1]!=2&&v.view.shape[1]!=3)||e.view.ndim!=2||e.view.shape[1]!=2||!v.format(plan.vertex_bytes,plan.vertex_integer)||!e.format(plan.edge_bytes,true)||v.view.shape[0]<0||e.view.shape[0]<0)return false;const Py_ssize_t n_edges=e.view.shape[0],n_vertices=v.view.shape[0];if(n_edges>0&&(!ac_polar_angles_addressable(e)||n_vertices==0))return false;if(n_vertices>0&&!ac_polar_angles_addressable(v))return false;for(Py_ssize_t i=0;i<n_edges;++i){const int64_t start=ac_reconstruct_edge_value(e,i,0,plan.edge_bytes),end=ac_reconstruct_edge_value(e,i,1,plan.edge_bytes);if(start<0||end<0||start>=n_vertices||end>=n_vertices)return false;}
 owner.put("n_edges",nb::int_(n_edges));nb::object visited=ac_call(plan.zeros,{E(nb::int_(n_edges))},{{"dtype",E(plan.bool_dtype)}});owner.put("visited",visited);AccelerationCentersBuffer marks;marks.acquire(visited);const char* format=marks.view.format;if(format&&(*format=='@'||*format=='='))++format;if(reinterpret_cast<PyObject*>(Py_TYPE(visited.ptr()))!=plan.ndarray_type.ptr()||marks.view.ndim!=1||marks.view.shape[0]!=n_edges||marks.view.itemsize!=1||!format||format[0]!='?'||format[1]!='\0'||marks.view.readonly||!marks.view.buf||!marks.view.strides||marks.view.suboffsets||(n_edges>0&&marks.view.strides[0]!=1))throw nb::type_error("native reconstruction zeros primitive changed source storage");
 owner.put("n_vertices",nb::int_(n_vertices));nb::object inner_type=J(plan.source_types,ONE),incident=ac_reconstruct_typed_empty(plan,J(plan.source_types,TWO));owner.put("incident",incident);for(Py_ssize_t vertex=0;vertex<n_vertices;++vertex){owner.put("_",nb::int_(vertex));{nb::object child=ac_reconstruct_typed_empty(plan,inner_type);ac_reconstruct_list_append(plan,incident,child);}}
 for(Py_ssize_t i=0;i<n_edges;++i){nb::object index=nb::int_(i);owner.put("i",index);const int64_t start_value=ac_reconstruct_edge_value(e,i,0,plan.edge_bytes);nb::object start=ac_reconstruct_edge_scalar(plan,start_value);owner.put("start",start);const int64_t end_value=ac_reconstruct_edge_value(e,i,1,plan.edge_bytes);nb::object end=ac_reconstruct_edge_scalar(plan,end_value);owner.put("end",end);{nb::object child=ac_reconstruct_list_item(plan,incident,start);ac_reconstruct_list_append(plan,child,index);}{nb::object child=ac_reconstruct_list_item(plan,incident,end);ac_reconstruct_list_append(plan,child,index);}}
 nb::object polygons=ac_reconstruct_typed_empty(plan,J(plan.source_types,nb::int_(6)));owner.put("polygons",polygons);auto was_visited=[&](Py_ssize_t index){unsigned char value;std::memcpy(&value,marks.address(index),1);return value!=0;};auto mark=[&](Py_ssize_t index){const unsigned char value=1;std::memcpy(marks.address(index),&value,1);};
 for(Py_ssize_t i=0;i<n_edges;++i){owner.put("i",nb::int_(i));if(was_visited(i))continue;nb::list poly;owner.put("poly",poly);const int64_t start_value=ac_reconstruct_edge_value(e,i,0,plan.edge_bytes);nb::object start=ac_reconstruct_edge_scalar(plan,start_value);owner.put("start_v",start);int64_t current_value=ac_reconstruct_edge_value(e,i,1,plan.edge_bytes);nb::object current=ac_reconstruct_edge_scalar(plan,current_value);owner.put("current_v",current);poly.append(start);poly.append(current);mark(i);bool closed=false;owner.put("closed",nb::bool_(false));
  while(!closed){bool found=false;owner.put("found",nb::bool_(false));nb::object iterator;{nb::object child=ac_reconstruct_list_item(plan,incident,current);iterator=ac_call(plan.iterate,{E(child)});}while(nb::object edge=ac_next(iterator)){owner.put("edge_idx",edge);const int64_t edge_index=PyLong_AsLongLong(edge.ptr());if(PyErr_Occurred())throw nb::python_error();if(edge_index<0||edge_index>=n_edges)throw nb::index_error("native reconstruction canonical incidence index changed");if(was_visited(static_cast<Py_ssize_t>(edge_index)))continue;const int64_t a_value=ac_reconstruct_edge_value(e,static_cast<Py_ssize_t>(edge_index),0,plan.edge_bytes);nb::object a=ac_reconstruct_edge_scalar(plan,a_value);owner.put("a",a);const int64_t b_value=ac_reconstruct_edge_value(e,static_cast<Py_ssize_t>(edge_index),1,plan.edge_bytes);nb::object b=ac_reconstruct_edge_scalar(plan,b_value);owner.put("b",b);const int64_t next_value=b_value==current_value?a_value:b_value;nb::object next=b_value==current_value?a:b;owner.put("next_v",next);mark(static_cast<Py_ssize_t>(edge_index));if(next_value==start_value){closed=true;owner.put("closed",nb::bool_(true));}else poly.append(next);current_value=next_value;current=next;owner.put("current_v",current);found=true;owner.put("found",nb::bool_(true));if(current_value==start_value){closed=true;owner.put("closed",nb::bool_(true));}break;}iterator.reset();if(!found)break;}
  // Source evaluates the saved vertices receiver, then np.array(poly), then
  // advanced indexing; its genuine primitive creates the writable fresh copy.
  nb::object receiver=vertices;nb::object index_array=ac_call(plan.array,{E(poly)});nb::object polygon=ac_item(receiver,index_array);index_array.reset();receiver.reset();owner.put("polygon",polygon);ac_reconstruct_list_append(plan,polygons,polygon);
 }result=polygons;return true;
}

// The incoming type objects come from Numba's typing phase. They are never
// inferred from Python scalar boxes; each lifted implementation retains them.
nb::object shape_accel_numba_signature(nb::object operation,nb::object argument_types,nb::dict globals){
 AccelerationGlobals ns(std::move(globals));std::string name=nb::cast<std::string>(ac_attr(operation,"literal_value"));ac_entry(name);
 nb::object arguments=ac_attr(argument_types,"types"),types=ns.get("types"),context=ac_attr(ns.get("_shape_numba_target"),"typing_context");
 auto type=[&](const char* key){return ac_attr(types,key);};
 auto array=[&](nb::object dtype,int ndim,const char* layout){return ac_call(type("Array"),{E(dtype),E(nb::int_(ndim)),E(nb::str(layout))});};
 auto dtype=[&](int index){return ac_attr(J(arguments,nb::int_(index)),"dtype");};
 auto tuple=[&](std::initializer_list<ACExpr> elements){return ac_call(type("Tuple"),{E(ac_tuple(elements))});};
 nb::object result,compile_metadata=arguments;
 if(name=="_dot"||name=="_cross_z"){
  nb::object multiply=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),"mul"))});
  nb::object product=ac_call(ac_attr(context,"resolve_function_type"),{E(multiply),E(T(E(dtype(0)),E(dtype(1)))),E(nb::dict())});
  if(product.is_none())throw nb::type_error("Numba cannot type native vector multiplication");
  nb::object scalar=ac_attr(product,"return_type");nb::object combine=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),name=="_dot"?"add":"sub"))});
  nb::object combined=ac_call(ac_attr(context,"resolve_function_type"),{E(combine),E(T(E(scalar),E(scalar))),E(nb::dict())});
  if(combined.is_none())throw nb::type_error("Numba cannot type native vector reduction");result=ac_attr(combined,"return_type");
  // Product/reduction policy is already resolved here, once per specialization.
  nb::object float32=type("float32"),float64=type("float64"),left_type=dtype(0),right_type=dtype(1);
  auto float_width=[&](nb::handle value){return value.ptr()==float32.ptr()?4:value.ptr()==float64.ptr()?8:0;};
  const int left_width=float_width(left_type),right_width=float_width(right_type),product_width=float_width(scalar),reduction_width=float_width(result);
  nb::object array_type=type("Array");
  auto compiler_array=[&](nb::object argument){int value=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(value<0)throw nb::python_error();return value!=0;};
  if(left_width&&right_width&&product_width&&reduction_width&&compiler_array(J(arguments,ZERO))&&compiler_array(J(arguments,ONE))&&nb::cast<int>(ac_attr(J(arguments,ZERO),"ndim"))==1&&nb::cast<int>(ac_attr(J(arguments,ONE),"ndim"))==1){
   nb::object ndarray_type=ac_attr(ns.get("np"),"ndarray");
   if(!PyType_Check(ndarray_type.ptr()))throw nb::type_error("native scalar typing requires the genuine ndarray type metadata");
   compile_metadata=ac_tuple({[&]{return nb::str(ac_scalar_float_plan_tag);},[&]{return arguments;},[&]{return ac_tuple({[&]{return nb::int_(left_width);},[&]{return nb::int_(right_width);},[&]{return nb::int_(product_width);},[&]{return nb::int_(reduction_width);}});},[&]{return ndarray_type;}});
  }
 }
 else if(name=="_sign_abs")result=tuple({E(type("float64")),E(type("float64"))});
 else if(name=="_orthogonal_vector")result=array(dtype(0),1,"C");
 else if(name=="_calc_output_size"){
  result=type("int64");nb::object array_type=type("Array"),argument=J(arguments,ZERO);int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();nb::object float32=type("float32"),float64=type("float64");auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};
  if(is_array&&J(arguments,ONE).ptr()==type("boolean").ptr()&&J(arguments,nb::int_(3)).ptr()==type("boolean").ptr()&&width(J(arguments,TWO))){nb::object input=ac_attr(argument,"dtype");if(width(input)&&nb::cast<int>(ac_attr(argument,"ndim"))==2){
   nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native output-size arithmetic");return sig;};
   nb::object row=ac_attr(resolve("getitem",argument,type("int64")),"return_type"),dot_types=tuple({E(row),E(row)}),dot_operation=ac_call(type("literal"),{[]{return nb::str("_dot");}}),dot_signature=shape_accel_numba_signature(dot_operation,dot_types,ns.values),dot_metadata=J(dot_signature,ONE);AccelerationScalarFloatPlan dot_plan;
   if(ac_scalar_float_plan_decode(dot_metadata,dot_plan)){nb::object dot_type=J(dot_signature,ZERO),compare=resolve("lt",dot_type,J(arguments,TWO)),compare_args=ac_attr(compare,"args");const int left=width(J(compare_args,ZERO)),right=width(J(compare_args,ONE));if(left&&right&&ac_attr(compare,"return_type").ptr()==type("boolean").ptr()){
    nb::object add=ac_attr(resolve("add",type("int64"),type("boolean")),"return_type"),sub=ac_attr(resolve("sub",type("int64"),type("boolean")),"return_type"),mul=ac_attr(resolve("mul",type("int64"),type("boolean")),"return_type");if(add.ptr()==type("int64").ptr()&&sub.ptr()==type("int64").ptr()&&mul.ptr()==type("int64").ptr()){nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),cos_dtype=ac_call(ns.get("_shape_numba_as_dtype"),{E(dot_type)}),cos_constructor=ac_attr(cos_dtype,"type");if(PyType_Check(ndarray_type.ptr())&&PyType_Check(cos_constructor.ptr())){nb::object widths=T(E(nb::int_(width(input))),E(nb::int_(width(J(arguments,TWO)))),E(nb::int_(left)),E(nb::int_(right)));compile_metadata=T(E(nb::str(ac_output_size_plan_tag)),E(arguments),E(widths),E(ndarray_type),E(dot_metadata),E(cos_constructor));}}
   }}
  }}
 }
 else if(name=="_set_centers_and_offsets"){
  result=type("int64");nb::object array_type=type("Array"),float32=type("float32"),float64=type("float64");
  auto compiler_array=[&](nb::object argument){int value=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(value<0)throw nb::python_error();return value!=0;};
  bool admitted=true;for(int i=0;i<6;++i)if(!compiler_array(J(arguments,nb::int_(i))))admitted=false;
  auto float_width=[&](nb::handle value){return value.ptr()==float32.ptr()?4:value.ptr()==float64.ptr()?8:0;};
  if(admitted){
   std::vector<int> widths;for(int i=0;i<6;++i){nb::object argument=J(arguments,nb::int_(i)),dt=ac_attr(argument,"dtype");const int width=i==2?(dt.ptr()==type("int32").ptr()?4:dt.ptr()==type("int64").ptr()?8:0):float_width(dt);widths.push_back(width);if(!width||nb::cast<int>(ac_attr(argument,"ndim"))!=(i<3?2:1))admitted=false;}
   for(int i:{6,7,9}){int width=float_width(J(arguments,nb::int_(i)));widths.push_back(width);if(!width)admitted=false;}
   if(J(arguments,nb::int_(8)).ptr()!=type("int64").ptr()||J(arguments,nb::int_(10)).ptr()!=type("boolean").ptr())admitted=false;
   if(admitted){
    nb::object operators=ns.get("_shape_numba_operator");
    auto resolve=[&](const char* op,nb::object left,nb::object right){nb::object function=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,op))});nb::object sig=ac_call(ac_attr(context,"resolve_function_type"),{E(function),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native centers arithmetic");return ac_attr(sig,"return_type");};
    nb::object input1=dtype(4),input2=dtype(5),product=resolve("mul",input1,input2),dot=resolve("add",product,product),cross=resolve("sub",product,product);
    nb::object division=resolve("truediv",type("int64"),cross),minimum=ac_call(ac_attr(context,"unify_types"),{E(float64),E(J(arguments,nb::int_(6))),E(J(arguments,nb::int_(7)))}),scale=resolve("mul",float64,minimum);
    nb::object scale_unified=ac_call(ac_attr(context,"unify_types"),{E(division),E(scale)});
    nb::object difference=resolve("sub",input1,input2),half=resolve("mul",difference,float64),miter_scalar=resolve("mul",half,scale_unified);
    nb::object difference_array=resolve("sub",J(arguments,nb::int_(4)),J(arguments,nb::int_(5))),half_array=resolve("mul",difference_array,float64),miter_array=resolve("mul",half_array,scale_unified);
    nb::object collinear_scalar=resolve("mul",float32,float64),collinear_array=resolve("mul",array(float32,1,"C"),float64),miter_unified=ac_call(ac_attr(context,"unify_types"),{E(collinear_array),E(miter_array)});
    nb::object invert_scalar=resolve("mul",float64,ac_attr(miter_unified,"dtype")),orth1_scalar=resolve("mul",float64,input1),orth2_scalar=resolve("mul",float64,input2);
    const std::vector<nb::object> precision_types={product,dot,cross,division,minimum,scale,difference,half,miter_scalar,ac_attr(miter_array,"dtype"),collinear_scalar,ac_attr(collinear_array,"dtype"),invert_scalar,orth1_scalar,orth2_scalar};for(const auto& value:precision_types){const int width=float_width(value);widths.push_back(width);if(!width)admitted=false;}
    if(ac_attr(miter_unified,"dtype").ptr()!=ac_attr(miter_array,"dtype").ptr()||ac_attr(miter_unified,"dtype").ptr()!=ac_attr(collinear_array,"dtype").ptr()||float_width(scale_unified)!=float_width(division)||float_width(scale_unified)!=float_width(scale))admitted=false;
    if(admitted){
     nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),empty=ac_attr(numpy,"empty");auto numpy_dtype=[&](nb::object dt){return ac_call(ns.get("_shape_numba_as_dtype"),{E(dt)});};auto constructor=[&](nb::object dt){return ac_attr(numpy_dtype(dt),"type");};
     if(PyType_Check(ndarray_type.ptr())&&PyCFunction_Check(empty.ptr())){
      nb::tuple width_tuple=nb::steal<nb::tuple>(PyTuple_New(24));if(!width_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<24;++i)PyTuple_SET_ITEM(width_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());
      nb::object constructors=T(E(constructor(dot)),E(constructor(cross)),E(constructor(scale_unified)),E(constructor(float64))),primitives=T(E(numpy_dtype(ac_attr(miter_unified,"dtype"))),E(empty));
      compile_metadata=T(E(nb::str(ac_centers_float_plan_tag)),E(arguments),E(width_tuple),E(ndarray_type),E(constructors),E(primitives));
     }
    }
   }
  }
 }
 else if(name=="_generate_2D_edge_meshes_loop"){
  result=type("none");nb::object array_type=type("Array"),float32=type("float32"),float64=type("float64");auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};auto compiler_array=[&](nb::object arg){int value=PyObject_IsInstance(arg.ptr(),array_type.ptr());if(value<0)throw nb::python_error();return value!=0;};bool admitted=true;for(int i:{0,4,5,6,7,8})if(!compiler_array(J(arguments,nb::int_(i))))admitted=false;
  if(J(arguments,ONE).ptr()!=type("boolean").ptr()||J(arguments,nb::int_(3)).ptr()!=type("boolean").ptr()||!width(J(arguments,TWO)))admitted=false;
  if(admitted){for(int i:{0,4,5,6,7,8})if(nb::cast<int>(ac_attr(J(arguments,nb::int_(i)),"ndim"))!=(i==5?1:2))admitted=false;for(int i:{0,4,5,6,7})if(!width(dtype(i)))admitted=false;nb::object triangle_type=dtype(8);if(triangle_type.ptr()!=type("int32").ptr()&&triangle_type.ptr()!=type("int64").ptr())admitted=false;
   if(admitted){nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native edge-loop arithmetic");return ac_attr(sig,"return_type");};
    nb::object vertex=resolve("getitem",J(arguments,ZERO),type("int64")),vector=resolve("getitem",J(arguments,nb::int_(4)),type("int64")),half_length=resolve("getitem",J(arguments,nb::int_(5)),type("int64"));
    nb::object child_arguments=T(E(J(arguments,nb::int_(6))),E(J(arguments,nb::int_(7))),E(J(arguments,nb::int_(8))),E(vertex),E(vector),E(vector),E(half_length),E(half_length),E(type("int64")),E(J(arguments,TWO)),E(J(arguments,nb::int_(3))));nb::object child_types=ac_call(type("Tuple"),{E(child_arguments)}),child_operation=ac_call(type("literal"),{[]{return nb::str("_set_centers_and_offsets");}}),child_signature=shape_accel_numba_signature(child_operation,child_types,ns.values),child_metadata=J(child_signature,ONE);AccelerationCentersFloatPlan child_plan;
    if(J(child_signature,ZERO).ptr()==type("int64").ptr()&&ac_centers_float_plan_decode(child_metadata,child_plan)){
     nb::object input=dtype(4),negative_op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,"neg"))}),negative_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(negative_op),E(T(E(input))),E(nb::dict())});if(negative_signature.is_none())throw nb::type_error("Numba cannot type native edge-loop orthogonal negation");nb::object negation=ac_attr(negative_signature,"return_type"),orthogonal=input,half_scalar=resolve("mul",float64,orthogonal),half_array=resolve("mul",float64,array(orthogonal,1,"C")),half_array_dtype=ac_attr(half_array,"dtype");const std::vector<nb::object> precision={dtype(0),input,dtype(5),negation,orthogonal,half_scalar,half_array_dtype};std::vector<int> widths;for(const auto& dt:precision){int value=width(dt);widths.push_back(value);if(!value)admitted=false;}
     if(width(negation)!=width(orthogonal)||width(half_scalar)!=width(half_array_dtype)||resolve("sub",type("int64"),type("boolean")).ptr()!=type("int64").ptr()||resolve("add",type("int64"),type("boolean")).ptr()!=type("int64").ptr()||resolve("add",type("int64"),J(child_signature,ZERO)).ptr()!=type("int64").ptr())admitted=false;
     if(admitted){nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),empty=ac_attr(numpy,"empty");if(PyType_Check(ndarray_type.ptr())&&PyCFunction_Check(empty.ptr())){auto numpy_dtype=[&](nb::object dt){return ac_call(ns.get("_shape_numba_as_dtype"),{E(dt)});};nb::tuple width_tuple=nb::steal<nb::tuple>(PyTuple_New(7));if(!width_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<7;++i)PyTuple_SET_ITEM(width_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());nb::object dtypes=T(E(numpy_dtype(orthogonal)),E(numpy_dtype(half_array_dtype)));compile_metadata=T(E(nb::str(ac_edge_loop_plan_tag)),E(arguments),E(width_tuple),E(ndarray_type),E(dtypes),E(empty),E(child_metadata));}}
    }
   }
  }
 }
 else if(name=="_normalize_triangle_orientation"){
  result=type("none");nb::object array_type=type("Array");auto compiler_array=[&](nb::object arg){int value=PyObject_IsInstance(arg.ptr(),array_type.ptr());if(value<0)throw nb::python_error();return value!=0;};bool admitted=true;for(int i=0;i<3;++i)if(!compiler_array(J(arguments,nb::int_(i))))admitted=false;
  if(admitted){nb::object float32=type("float32"),float64=type("float64");auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};nb::object triangle_type=dtype(0),center_type=dtype(1),offset_type=dtype(2);const int triangle_width=triangle_type.ptr()==type("int32").ptr()?4:triangle_type.ptr()==type("int64").ptr()?8:0;std::vector<int> widths={triangle_width,width(center_type),width(offset_type)};
   for(int i=0;i<3;++i)if(!widths[static_cast<size_t>(i)]||nb::cast<int>(ac_attr(J(arguments,nb::int_(i)),"ndim"))!=2)admitted=false;
   if(admitted){nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native triangle normalization arithmetic");return ac_attr(sig,"return_type");};
    nb::object addition=resolve("add",center_type,offset_type),point_array=resolve("add",array(center_type,1,"A"),array(offset_type,1,"A")),point_type=ac_attr(point_array,"dtype"),subtraction=resolve("sub",point_type,point_type),product=resolve("mul",subtraction,subtraction),reduction=resolve("sub",product,product);const std::vector<nb::object> precision={addition,point_type,subtraction,product,reduction};for(const auto& dt:precision){int value=width(dt);widths.push_back(value);if(!value)admitted=false;}if(width(addition)!=width(point_type))admitted=false;
    nb::object enum_type=ns.get("Orientation"),collinear=ac_attr(enum_type,"collinear"),enum_member=ac_call(ac_attr(context,"resolve_value_type"),{E(collinear)});int is_enum=PyObject_IsInstance(enum_member.ptr(),type("EnumMember").ptr());if(is_enum<0)throw nb::python_error();if(!is_enum||!PyType_Check(enum_type.ptr())||ac_attr(enum_member,"instance_class").ptr()!=enum_type.ptr())admitted=false;
    if(admitted){nb::object clockwise=ac_attr(enum_type,"clockwise"),anticlockwise=ac_attr(enum_type,"anticlockwise");if(reinterpret_cast<PyObject*>(Py_TYPE(clockwise.ptr()))!=enum_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(anticlockwise.ptr()))!=enum_type.ptr()||reinterpret_cast<PyObject*>(Py_TYPE(collinear.ptr()))!=enum_type.ptr())admitted=false;
     if(admitted){nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),empty=ac_attr(numpy,"empty");auto numpy_dtype=[&](nb::object dt){return ac_call(ns.get("_shape_numba_as_dtype"),{E(dt)});};nb::object point_dtype=numpy_dtype(point_type),val_constructor=ac_attr(numpy_dtype(reduction),"type");if(PyType_Check(ndarray_type.ptr())&&PyCFunction_Check(empty.ptr())&&PyType_Check(val_constructor.ptr())){
      nb::tuple width_tuple=nb::steal<nb::tuple>(PyTuple_New(8));if(!width_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<8;++i)PyTuple_SET_ITEM(width_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());nb::object members=T(E(clockwise),E(anticlockwise),E(collinear));compile_metadata=T(E(nb::str(ac_normalize_triangles_plan_tag)),E(arguments),E(width_tuple),E(ndarray_type),E(point_dtype),E(empty),E(enum_type),E(members),E(val_constructor));
     }}
    }
   }
  }
 }
 else if(name=="_orientation"){
  nb::object orientation=ns.get("Orientation"),collinear=ac_attr(orientation,"collinear");result=ac_call(ac_attr(context,"resolve_value_type"),{E(collinear)});
  nb::object array_type=type("Array");
  auto compiler_array=[&](nb::object argument){int value=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(value<0)throw nb::python_error();return value!=0;};
  // Admission precedes every array-only attribute read (including typed.List).
  if(compiler_array(J(arguments,ZERO))&&compiler_array(J(arguments,ONE))&&compiler_array(J(arguments,TWO))){
   nb::object float32=type("float32"),float64=type("float64"),input1=dtype(0),input2=dtype(1),input3=dtype(2);
   auto float_width=[&](nb::handle value){return value.ptr()==float32.ptr()?4:value.ptr()==float64.ptr()?8:0;};
   const int input1_width=float_width(input1),input2_width=float_width(input2),input3_width=float_width(input3);
   if(input1_width&&input2_width&&input3_width&&nb::cast<int>(ac_attr(J(arguments,ZERO),"ndim"))==1&&nb::cast<int>(ac_attr(J(arguments,ONE),"ndim"))==1&&nb::cast<int>(ac_attr(J(arguments,TWO),"ndim"))==1){
    nb::object subtract=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),"sub"))});
    nb::object multiply=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),"mul"))});
    auto resolve=[&](nb::object op,nb::object left,nb::object right){nb::object signature=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(signature.is_none())throw nb::type_error("Numba cannot type native orientation arithmetic");return ac_attr(signature,"return_type");};
    nb::object sub21=resolve(subtract,input2,input1),sub31=resolve(subtract,input3,input1),product=resolve(multiply,sub21,sub31),reduction=resolve(subtract,product,product);
    const int sub21_width=float_width(sub21),sub31_width=float_width(sub31),product_width=float_width(product),reduction_width=float_width(reduction);
    int enum_member=PyObject_IsInstance(result.ptr(),type("EnumMember").ptr());if(enum_member<0)throw nb::python_error();
    if(sub21_width&&sub31_width&&product_width&&reduction_width&&enum_member&&PyType_Check(orientation.ptr())&&ac_attr(result,"instance_class").ptr()==orientation.ptr()){
     nb::object clockwise=ac_attr(orientation,"clockwise"),anticlockwise=ac_attr(orientation,"anticlockwise");
     if(reinterpret_cast<PyObject*>(Py_TYPE(clockwise.ptr()))==orientation.ptr()&&reinterpret_cast<PyObject*>(Py_TYPE(anticlockwise.ptr()))==orientation.ptr()&&reinterpret_cast<PyObject*>(Py_TYPE(collinear.ptr()))==orientation.ptr()){
      nb::object ndarray_type=ac_attr(ns.get("np"),"ndarray"),value_dtype=ac_call(ns.get("_shape_numba_as_dtype"),{E(reduction)}),val_constructor=ac_attr(value_dtype,"type");
      if(!PyType_Check(ndarray_type.ptr())||!PyType_Check(val_constructor.ptr()))throw nb::type_error("native orientation typing requires genuine ndarray/scalar type metadata");
      nb::tuple widths=ac_tuple({[&]{return nb::int_(input1_width);},[&]{return nb::int_(input2_width);},[&]{return nb::int_(input3_width);},[&]{return nb::int_(sub21_width);},[&]{return nb::int_(sub31_width);},[&]{return nb::int_(product_width);},[&]{return nb::int_(reduction_width);}});
      nb::tuple members=ac_tuple({[&]{return clockwise;},[&]{return anticlockwise;},[&]{return collinear;}});
      compile_metadata=ac_tuple({[&]{return nb::str(ac_orientation_float_plan_tag);},[&]{return arguments;},[&]{return widths;},[&]{return ndarray_type;},[&]{return orientation;},[&]{return members;},[&]{return val_constructor;}});
     }
    }
   }
  }
 }
 else if(name=="is_convex"){
  result=type("boolean");nb::object argument=J(arguments,ZERO),array_type=type("Array");int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();if(is_array&&nb::cast<int>(ac_attr(argument,"ndim"))==2){nb::object float32=type("float32"),float64=type("float64"),input=ac_attr(argument,"dtype");auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};if(width(input)){
   nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native convex operations");return sig;};auto child=[&](const char* name,nb::object args){return shape_accel_numba_signature(ac_call(type("literal"),{[&]{return nb::str(name);}}),args,ns.values);};
   nb::object zero=ac_call(type("literal"),{[]{return nb::int_(0);}}),row=ac_attr(resolve("getitem",argument,type("int64")),"return_type"),column=ac_attr(resolve("getitem",argument,tuple({E(type("slice2_type")),E(zero)})),"return_type");
   nb::object orientation_signature=child("_orientation",tuple({E(row),E(row),E(row)})),orientation_metadata=J(orientation_signature,ONE);AccelerationOrientationFloatPlan orientation;
   nb::object centroid=array(float32,1,"C"),reversed=ac_attr(resolve("getitem",argument,type("slice3_type")),"return_type"),forward_signature=child("_are_polar_angles_monotonic",tuple({E(argument),E(centroid)})),reverse_signature=child("_are_polar_angles_monotonic",tuple({E(reversed),E(centroid)}));AccelerationPolarAnglesPlan forward_plan,reverse_plan;
   nb::object numpy=ns.get("np"),dtype=ac_call(ns.get("_shape_numba_as_dtype"),{E(input)}),constructor=ac_attr(dtype,"type"),ctor_type=ac_call(ac_attr(context,"resolve_value_type"),{E(constructor)}),init_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(ctor_type),E(T(E(zero))),E(nb::dict())}),nditer_type=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(numpy,"nditer"))}),nditer_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(nditer_type),E(T(E(column))),E(nb::dict())}),mean_type=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(numpy,"mean"))}),mean_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(mean_type),E(T(E(column))),E(nb::dict())});
   if(!init_signature.is_none()&&!nditer_signature.is_none()&&!mean_signature.is_none()&&ac_orientation_float_plan_decode(orientation_metadata,orientation)&&ac_polar_angles_plan_decode(J(forward_signature,ONE),forward_plan)&&ac_polar_angles_plan_decode(J(reverse_signature,ONE),reverse_plan)){
    nb::object iterator=ac_attr(nditer_signature,"return_type"),view=ac_attr(iterator,"yield_type"),item_type=ac_call(ac_attr(context,"resolve_getattr"),{E(view),[]{return nb::str("item");}}),item_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(item_type),E(T()),E(nb::dict())});if(!item_signature.is_none()){
     nb::object initial=ac_attr(init_signature,"return_type"),item=ac_attr(item_signature,"return_type"),add=resolve("iadd",initial,item),add_args=ac_attr(add,"args"),sum=ac_attr(add,"return_type"),divide=resolve("truediv",sum,type("intp")),divide_args=ac_attr(divide,"args"),divided=ac_attr(divide,"return_type"),mean=ac_attr(mean_signature,"return_type");const std::vector<nb::object> precision={input,initial,J(add_args,ZERO),J(add_args,ONE),sum,J(divide_args,ZERO),J(divide_args,ONE),divided,mean,float32};std::vector<int> widths;bool admitted=true;for(const auto& dt:precision){const int value=width(dt);widths.push_back(value);if(!value)admitted=false;}if(widths[0]!=widths[1]||widths[1]!=widths[8]||widths[2]!=widths[3]||widths[3]!=widths[4]||widths[5]!=widths[6]||widths[6]!=widths[7])admitted=false;
     nb::object layout=ac_attr(iterator,"layout"),indexers=ac_attr(iterator,"indexers"),indexer=J(indexers,ZERO),kind=J(indexer,ZERO);if(nb::cast<int>(ac_attr(iterator,"ndim"))!=1||nb::cast<std::string>(layout)!="C"||nb::cast<int>(J(indexer,ONE))!=0||nb::cast<int>(J(indexer,TWO))!=1||(nb::cast<std::string>(kind)!="flat"&&nb::cast<std::string>(kind)!="indexed"))admitted=false;
     if(admitted){nb::object ndarray_type=ac_attr(numpy,"ndarray"),float32_dtype=ac_attr(numpy,"float32"),empty=ac_attr(numpy,"empty"),mean_constructor=ac_attr(ac_call(ns.get("_shape_numba_as_dtype"),{E(mean)}),"type");if(PyType_Check(ndarray_type.ptr())&&PyType_Check(float32_dtype.ptr())&&PyType_Check(mean_constructor.ptr())&&PyCFunction_Check(empty.ptr())){nb::tuple widths_tuple=nb::steal<nb::tuple>(PyTuple_New(10));if(!widths_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<10;++i)PyTuple_SET_ITEM(widths_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());nb::object declarations=T(E(float32_dtype),E(empty),E(mean_constructor)),iteration=T(E(layout),E(nb::int_(1)),E(kind));compile_metadata=T(E(nb::str(ac_convex_plan_tag)),E(arguments),E(widths_tuple),E(ndarray_type),E(declarations),E(iteration),E(orientation_metadata),E(J(forward_signature,ONE)),E(J(reverse_signature,ONE)));}}
    }
   }
  }}
 }
 else if(name=="_are_polar_angles_monotonic"){
  result=type("boolean");nb::object array_type=type("Array");auto compiler_array=[&](nb::object arg){int value=PyObject_IsInstance(arg.ptr(),array_type.ptr());if(value<0)throw nb::python_error();return value!=0;};
  if(compiler_array(J(arguments,ZERO))&&compiler_array(J(arguments,ONE))){nb::object float32=type("float32"),float64=type("float64");auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};nb::object vertices=dtype(0),centroid=dtype(1),atan2=ns.get("atan2"),pi=ns.get("pi");if(width(vertices)&&width(centroid)&&nb::cast<int>(ac_attr(J(arguments,ZERO),"ndim"))==2&&nb::cast<int>(ac_attr(J(arguments,ONE),"ndim"))==1&&PyCFunction_Check(atan2.ptr())&&PyFloat_CheckExact(pi.ptr())&&nb::cast<std::string>(ac_attr(atan2,"__name__"))=="atan2"&&nb::cast<std::string>(ac_attr(atan2,"__module__"))=="math"){
   nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native polar-angle arithmetic");return ac_attr(sig,"return_type");};
   nb::object difference=resolve("sub",vertices,centroid),atan2_type=ac_call(ac_attr(context,"resolve_value_type"),{E(atan2)}),atan2_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(atan2_type),E(T(E(difference),E(difference))),E(nb::dict())});if(atan2_signature.is_none())throw nb::type_error("Numba cannot type native polar-angle atan2");nb::object atan2_args=ac_attr(atan2_signature,"args"),atan2_argument=J(atan2_args,ZERO),start_type=ac_attr(atan2_signature,"return_type"),angle_type=resolve("sub",start_type,start_type),pi_type=ac_call(ac_attr(context,"resolve_value_type"),{E(pi)}),two_pi_type=resolve("mul",type("int64"),pi_type),wrapped_type=resolve("iadd",angle_type,two_pi_type),merged_type=ac_call(ac_attr(context,"unify_types"),{E(angle_type),E(wrapped_type)}),previous_type=ac_call(ac_attr(context,"unify_types"),{E(float64),E(merged_type)});
   const std::vector<nb::object> precision={vertices,centroid,difference,atan2_argument,start_type,angle_type,two_pi_type,wrapped_type,merged_type,previous_type};std::vector<int> widths;bool admitted=true;for(const auto& dt:precision){int value=width(dt);widths.push_back(value);if(!value)admitted=false;}if(width(atan2_argument)!=width(J(atan2_args,ONE))||width(wrapped_type)!=width(merged_type)||width(merged_type)!=width(previous_type))admitted=false;
   nb::object zero=ac_call(type("literal"),{[]{return nb::int_(0);}});if(resolve("lt",angle_type,zero).ptr()!=type("boolean").ptr()||resolve("lt",merged_type,previous_type).ptr()!=type("boolean").ptr())admitted=false;
   if(admitted){nb::object ndarray_type=ac_attr(ns.get("np"),"ndarray");auto constructor=[&](nb::object dt){return ac_attr(ac_call(ns.get("_shape_numba_as_dtype"),{E(dt)}),"type");};nb::object constructors=T(E(constructor(start_type)),E(constructor(angle_type)),E(constructor(merged_type)),E(constructor(previous_type)));if(PyType_Check(ndarray_type.ptr())){nb::tuple width_tuple=nb::steal<nb::tuple>(PyTuple_New(10));if(!width_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<10;++i)PyTuple_SET_ITEM(width_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());compile_metadata=T(E(nb::str(ac_polar_angles_plan_tag)),E(arguments),E(width_tuple),E(ndarray_type),E(pi),E(constructors),E(atan2));}}
  }}
 }
 else if(name=="_direction_vec_and_half_length"){
  nb::object layout=ac_attr(J(arguments,ZERO),"layout");std::string order=nb::cast<std::string>(layout);nb::object normals=array(dtype(0),2,order=="F"?"F":"C");
  nb::object multiply_type=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),"mul"))});
  nb::object signature=ac_call(ac_attr(context,"resolve_function_type"),{E(multiply_type),E(T(E(array(type("float32"),1,"C")),E(type("float64")))),E(nb::dict())});
  if(signature.is_none())throw nb::type_error("Numba cannot type direction-length multiplication");result=tuple({E(normals),E(ac_attr(signature,"return_type"))});
  nb::object argument=J(arguments,ZERO),array_type=type("Array");int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();
  if(is_array&&J(arguments,ONE).ptr()==type("boolean").ptr()){
   nb::object float32=type("float32"),float64=type("float64"),input=ac_attr(argument,"dtype");auto width=[&](nb::handle value){return value.ptr()==float32.ptr()?4:value.ptr()==float64.ptr()?8:0;};
   if(width(input)&&nb::cast<int>(ac_attr(argument,"ndim"))==2){
    nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))});nb::object sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native directions arithmetic");return ac_attr(sig,"return_type");};
    nb::object difference=resolve("sub",input,input),row=array(input,1,order=="C"?"C":"A"),difference_array=resolve("sub",row,row),difference_dtype=ac_attr(difference_array,"dtype");
    nb::object exponent=ac_call(type("literal"),{[]{return nb::int_(2);}}),power=resolve("pow",difference_dtype,exponent),sum=resolve("add",power,power);
    nb::object sqrt_function=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("np"),"sqrt"))}),sqrt_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(sqrt_function),E(T(E(sum))),E(nb::dict())});if(sqrt_signature.is_none())throw nb::type_error("Numba cannot type native directions sqrt");nb::object root_type=ac_attr(sqrt_signature,"return_type");
    nb::object division=resolve("truediv",difference_dtype,float32),normal_array=resolve("truediv",difference_array,float32),normal_dtype=ac_attr(normal_array,"dtype"),half_scalar=resolve("mul",float32,float64),half_dtype=ac_attr(ac_attr(signature,"return_type"),"dtype");
    const std::vector<nb::object> precision_types={input,difference,difference_dtype,power,sum,root_type,division,normal_dtype,half_scalar,half_dtype};std::vector<int> widths;bool admitted=true;for(const auto& dt:precision_types){int value=width(dt);widths.push_back(value);if(!value)admitted=false;}
    if(width(difference)!=width(difference_dtype)||width(normal_dtype)!=width(division)||width(half_scalar)!=width(half_dtype))admitted=false;
    if(admitted){nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),empty_like=ac_attr(numpy,"empty_like"),empty=ac_attr(numpy,"empty");if(PyType_Check(ndarray_type.ptr())&&PyCallable_Check(empty_like.ptr())&&PyCFunction_Check(empty.ptr())){
     auto numpy_dtype=[&](nb::object dt){return ac_call(ns.get("_shape_numba_as_dtype"),{E(dt)});};nb::tuple width_tuple=nb::steal<nb::tuple>(PyTuple_New(10));if(!width_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<10;++i)PyTuple_SET_ITEM(width_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());
     nb::object dtypes=T(E(numpy_dtype(float32)),E(numpy_dtype(difference_dtype)),E(numpy_dtype(half_dtype))),primitives=T(E(empty_like),E(empty));compile_metadata=T(E(nb::str(ac_directions_float_plan_tag)),E(arguments),E(width_tuple),E(ndarray_type),E(nb::str(order=="F"?"F":"C")),E(dtypes),E(primitives));
    }}
   }
  }
 }
 else if(name=="_cut_end_if_repetition"){
  std::string order=nb::cast<std::string>(ac_attr(J(arguments,ZERO),"layout"));
  nb::object converted=array(type("float32"),2,order=="C"?"C":order=="F"?"F":"A");
  nb::object getitem=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),"getitem"))});
  nb::object sliced=ac_call(ac_attr(context,"resolve_function_type"),{E(getitem),E(T(E(converted),E(type("slice2_type")))),E(nb::dict())});
  if(sliced.is_none())throw nb::type_error("Numba cannot type native path slicing");
  result=ac_call(ac_attr(context,"unify_types"),{E(converted),E(ac_attr(sliced,"return_type"))});
 }
 else if(name=="generate_2D_edge_meshes"){
  result=tuple({E(array(type("float32"),2,"C")),E(array(type("float32"),2,"C")),E(array(type("int32"),2,"C"))});nb::object argument=J(arguments,ZERO),array_type=type("Array");int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();if(is_array&&nb::cast<int>(ac_attr(argument,"ndim"))==2){nb::object float32=type("float32"),float64=type("float64"),input=ac_attr(argument,"dtype"),closed=ac_call(type("unliteral"),{E(J(arguments,ONE))}),limit=ac_call(type("unliteral"),{E(J(arguments,TWO))}),bevel=ac_call(type("unliteral"),{E(J(arguments,nb::int_(3)))});auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};if(width(input)&&width(limit)&&closed.ptr()==type("boolean").ptr()&&bevel.ptr()==type("boolean").ptr()){
   nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native edge-owner operations");return sig;};auto child=[&](const char* name,nb::object args){return shape_accel_numba_signature(ac_call(type("literal"),{[&]{return nb::str(name);}}),args,ns.values);};nb::object one=ac_call(type("literal"),{[]{return nb::int_(1);}}),two=ac_call(type("literal"),{[]{return nb::int_(2);}});
   nb::object half_signature=resolve("truediv",limit,two),half_args=ac_attr(half_signature,"args"),half=ac_attr(half_signature,"return_type"),power_signature=resolve("pow",half,two),power_args=ac_attr(power_signature,"args"),power=ac_attr(power_signature,"return_type"),multiply_signature=resolve("mul",two,power),multiply_args=ac_attr(multiply_signature,"args"),denominator=ac_attr(multiply_signature,"return_type"),reciprocal_signature=resolve("truediv",one,denominator),reciprocal_args=ac_attr(reciprocal_signature,"args"),reciprocal=ac_attr(reciprocal_signature,"return_type"),subtract_signature=resolve("sub",reciprocal,float64),subtract_args=ac_attr(subtract_signature,"args"),cosine=ac_attr(subtract_signature,"return_type");
   const std::vector<nb::object> precision={input,limit,J(half_args,ZERO),J(half_args,ONE),half,J(power_args,ZERO),power,J(multiply_args,ZERO),J(multiply_args,ONE),denominator,J(reciprocal_args,ZERO),J(reciprocal_args,ONE),reciprocal,J(subtract_args,ZERO),J(subtract_args,ONE),cosine};std::vector<int> widths;bool admitted=true;for(const auto& dt:precision){const int value=width(dt);widths.push_back(value);if(!value)admitted=false;}if(widths[2]!=widths[3]||widths[3]!=widths[4]||widths[5]!=widths[6]||widths[7]!=widths[8]||widths[8]!=widths[9]||widths[10]!=widths[11]||widths[11]!=widths[12]||widths[13]!=widths[14]||widths[14]!=widths[15])admitted=false;
   nb::object trim_signature=child("_cut_end_if_repetition",tuple({E(argument)})),converted=J(trim_signature,ZERO),direction_signature=child("_direction_vec_and_half_length",tuple({E(converted),E(closed)})),direction_types=ac_attr(J(direction_signature,ZERO),"types"),directions=J(direction_types,ZERO),lengths=J(direction_types,ONE),size_signature=child("_calc_output_size",tuple({E(directions),E(closed),E(cosine),E(bevel)})),centers=array(float32,2,"C"),offsets=array(float32,2,"C"),triangles=array(type("int32"),2,"C"),loop_signature=child("_generate_2D_edge_meshes_loop",tuple({E(converted),E(closed),E(cosine),E(bevel),E(directions),E(lengths),E(centers),E(offsets),E(triangles)})),normalize_signature=child("_normalize_triangle_orientation",tuple({E(triangles),E(centers),E(offsets)}));AccelerationDirectionsFloatPlan direction_plan;AccelerationOutputSizePlan size_plan;AccelerationEdgeLoopPlan loop_plan;AccelerationNormalizeTrianglesPlan normalize_plan;
   if(admitted&&ac_directions_float_plan_decode(J(direction_signature,ONE),direction_plan)&&ac_output_size_plan_decode(J(size_signature,ONE),size_plan)&&ac_edge_loop_plan_decode(J(loop_signature,ONE),loop_plan)&&ac_normalize_triangles_plan_decode(J(normalize_signature,ONE),normalize_plan)){
    nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),asarray=ac_attr(numpy,"asarray"),empty=ac_attr(numpy,"empty"),zeros=ac_attr(numpy,"zeros"),float32_dtype=ac_attr(numpy,"float32"),int32_dtype=ac_attr(numpy,"int32"),cos_constructor=ac_attr(ac_call(ns.get("_shape_numba_as_dtype"),{E(cosine)}),"type");if(PyType_Check(ndarray_type.ptr())&&PyCFunction_Check(asarray.ptr())&&PyCFunction_Check(empty.ptr())&&PyCFunction_Check(zeros.ptr())&&PyType_Check(float32_dtype.ptr())&&PyType_Check(int32_dtype.ptr())&&PyType_Check(cos_constructor.ptr())){nb::tuple widths_tuple=nb::steal<nb::tuple>(PyTuple_New(16));if(!widths_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<16;++i)PyTuple_SET_ITEM(widths_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());nb::object declarations=T(E(asarray),E(empty),E(zeros),E(float32_dtype),E(int32_dtype)),children=T(E(J(direction_signature,ONE)),E(J(size_signature,ONE)),E(J(loop_signature,ONE)),E(J(normalize_signature,ONE)));compile_metadata=T(E(nb::str(ac_edge_owner_plan_tag)),E(arguments),E(widths_tuple),E(ndarray_type),E(declarations),E(cos_constructor),E(children),E(converted));}
   }
  }}
 }
 else if(name=="remove_path_duplicates"){
  result=ac_call(ac_attr(context,"unify_types"),{E(J(arguments,ZERO)),E(array(dtype(0),2,"C"))});nb::object argument=J(arguments,ZERO),array_type=type("Array");int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();if(is_array&&nb::cast<int>(ac_attr(argument,"ndim"))==2&&ac_call(type("unliteral"),{E(J(arguments,ONE))}).ptr()==type("boolean").ptr()){nb::object input=ac_attr(argument,"dtype");const int width=input.ptr()==type("float32").ptr()?4:input.ptr()==type("float64").ptr()?8:0;if(width){nb::object numpy=ns.get("np"),ndarray=ac_attr(numpy,"ndarray"),empty=ac_attr(numpy,"empty"),numpy_dtype=ac_call(ns.get("_shape_numba_as_dtype"),{E(input)});if(PyType_Check(ndarray.ptr())&&PyCFunction_Check(empty.ptr()))compile_metadata=T(E(nb::str(ac_dedup_plan_tag)),E(arguments),E(nb::int_(width)),E(ndarray),E(empty),E(numpy_dtype),E(result));}}
 }
 else if(name=="create_box_from_bounding"){
  result=array(type("float32"),2,"C");nb::object argument=J(arguments,ZERO),array_type=type("Array");int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();
  if(is_array&&nb::cast<int>(ac_attr(argument,"ndim"))==2){nb::object float32=type("float32"),float64=type("float64"),input=ac_attr(argument,"dtype");auto width=[&](nb::handle dt){return dt.ptr()==float32.ptr()?4:dt.ptr()==float64.ptr()?8:0;};if(width(input)){
   nb::object operators=ns.get("_shape_numba_operator");auto resolve=[&](const char* name,nb::object left,nb::object right){nb::object op=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(operators,name))}),sig=ac_call(ac_attr(context,"resolve_function_type"),{E(op),E(T(E(left),E(right))),E(nb::dict())});if(sig.is_none())throw nb::type_error("Numba cannot type native bounding-box operations");return sig;};
   nb::object zero=ac_call(type("literal"),{[]{return nb::int_(0);}}),one=ac_call(type("literal"),{[]{return nb::int_(1);}}),two=ac_call(type("literal"),{[]{return nb::int_(2);}});
   nb::object scalar=ac_attr(resolve("getitem",argument,tuple({E(zero),E(zero)})),"return_type");bool admitted=true;for(nb::object index:{tuple({E(one),E(zero)}),tuple({E(zero),E(one)}),tuple({E(one),E(one)})})if(ac_attr(resolve("getitem",argument,index),"return_type").ptr()!=scalar.ptr())admitted=false;
   nb::object add=resolve("add",scalar,scalar),add_args=ac_attr(add,"args"),sum=ac_attr(add,"return_type"),divide=resolve("truediv",sum,two),divide_args=ac_attr(divide,"args"),midpoint=ac_attr(divide,"return_type");
   const std::vector<nb::object> precision={input,scalar,J(add_args,ZERO),J(add_args,ONE),sum,J(divide_args,ZERO),J(divide_args,ONE),midpoint,float32};std::vector<int> widths;for(const auto& dt:precision){const int value=width(dt);widths.push_back(value);if(!value)admitted=false;}if(widths[0]!=widths[1]||widths[2]!=widths[3]||widths[3]!=widths[4]||widths[5]!=widths[6]||widths[6]!=widths[7])admitted=false;
   if(admitted){nb::object numpy=ns.get("np"),ndarray_type=ac_attr(numpy,"ndarray"),empty=ac_attr(numpy,"empty"),scalar_dtype=ac_call(ns.get("_shape_numba_as_dtype"),{E(scalar)}),scalar_constructor=ac_attr(scalar_dtype,"type"),float32_dtype=ac_attr(numpy,"float32");if(PyType_Check(ndarray_type.ptr())&&PyType_Check(scalar_constructor.ptr())&&PyType_Check(float32_dtype.ptr())&&PyCFunction_Check(empty.ptr())){nb::tuple width_tuple=nb::steal<nb::tuple>(PyTuple_New(9));if(!width_tuple.is_valid())throw nb::python_error();for(Py_ssize_t i=0;i<9;++i)PyTuple_SET_ITEM(width_tuple.ptr(),i,nb::int_(widths[static_cast<size_t>(i)]).release().ptr());nb::object declarations=T(E(scalar_constructor),E(float32_dtype));compile_metadata=T(E(nb::str(ac_bounding_box_plan_tag)),E(arguments),E(width_tuple),E(ndarray_type),E(declarations),E(empty));}}
  }}
 }
 else if(name=="reconstruct_polygons_from_edges"){
  result=ac_call(type("ListType"),{E(array(dtype(0),2,"C"))});nb::object vertex=J(arguments,ZERO),edge=J(arguments,ONE),array_type=type("Array");int vertex_array=PyObject_IsInstance(vertex.ptr(),array_type.ptr()),edge_array=PyObject_IsInstance(edge.ptr(),array_type.ptr());if(vertex_array<0||edge_array<0)throw nb::python_error();if(vertex_array&&edge_array&&nb::cast<int>(ac_attr(vertex,"ndim"))==2&&nb::cast<int>(ac_attr(edge,"ndim"))==2&&nb::cast<int>(ac_attr(type("intp"),"bitwidth"))==64){nb::object vertex_dtype=ac_attr(vertex,"dtype"),edge_dtype=ac_attr(edge,"dtype");const bool vertex_integer=vertex_dtype.ptr()==type("int32").ptr()||vertex_dtype.ptr()==type("int64").ptr();const int vertex_bytes=vertex_dtype.ptr()==type("float32").ptr()||vertex_dtype.ptr()==type("int32").ptr()?4:vertex_dtype.ptr()==type("float64").ptr()||vertex_dtype.ptr()==type("int64").ptr()?8:0,edge_bytes=edge_dtype.ptr()==type("int32").ptr()?4:edge_dtype.ptr()==type("int64").ptr()?8:0;if(vertex_bytes&&edge_bytes){
   nb::object numpy=ns.get("np"),list_class=ns.get("List"),ndarray_type=ac_attr(numpy,"ndarray"),zeros=ac_attr(numpy,"zeros"),numpy_array=ac_attr(numpy,"array"),bool_dtype=ac_attr(numpy,"bool_"),edge_constructor=ac_attr(ac_call(ns.get("_shape_numba_as_dtype"),{E(edge_dtype)}),"type"),poly_type=ac_call(type("List"),{E(edge_dtype)}),array_function_type=ac_call(ac_attr(context,"resolve_value_type"),{E(numpy_array)}),index_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(array_function_type),E(T(E(poly_type))),E(nb::dict())});if(index_signature.is_none())throw nb::type_error("Numba cannot type source reconstruction np.array(poly)");nb::object index_type=ac_attr(index_signature,"return_type"),getitem_function_type=ac_call(ac_attr(context,"resolve_value_type"),{E(ac_attr(ns.get("_shape_numba_operator"),"getitem"))}),gather_signature=ac_call(ac_attr(context,"resolve_function_type"),{E(getitem_function_type),E(T(E(vertex),E(index_type))),E(nb::dict())});if(gather_signature.is_none())throw nb::type_error("Numba cannot type source reconstruction advanced gather");nb::object polygon_type=ac_attr(gather_signature,"return_type"),element_type=ac_attr(result,"item_type");const int compatible=PyObject_RichCompareBool(polygon_type.ptr(),element_type.ptr(),Py_EQ);if(compatible<0)throw nb::python_error();
   if(compatible&&PyType_Check(ndarray_type.ptr())&&PyType_Check(list_class.ptr())&&PyCFunction_Check(zeros.ptr())&&PyCFunction_Check(numpy_array.ptr())&&PyType_Check(bool_dtype.ptr())&&PyType_Check(edge_constructor.ptr())){nb::object empty=ac_attr(list_class,"empty_list"),append=ac_attr(list_class,"append"),getitem=ac_attr(list_class,"__getitem__"),iterate=ac_attr(list_class,"__iter__");if(PyMethod_Check(empty.ptr())&&PyFunction_Check(append.ptr())&&PyFunction_Check(getitem.ptr())&&PyFunction_Check(iterate.ptr())&&nb::cast<std::string>(ac_attr(list_class,"__module__"))=="numba.typed.typedlist"&&nb::cast<std::string>(ac_attr(list_class,"__name__"))=="List"){nb::object inner=ac_call(type("ListType"),{E(type("int64"))}),incident=ac_call(type("ListType"),{E(inner)}),visited=array(type("boolean"),1,"C"),source_types=T(E(visited),E(inner),E(incident),E(poly_type),E(index_type),E(polygon_type),E(result));compile_metadata=T(E(nb::str(ac_reconstruct_plan_tag)),E(arguments),E(T(E(nb::bool_(vertex_integer)),E(nb::int_(vertex_bytes)),E(nb::int_(edge_bytes)),E(nb::int_(64)))),E(T(E(ndarray_type),E(list_class))),E(T(E(zeros),E(bool_dtype),E(numpy_array),E(edge_constructor))),E(source_types),E(T(E(empty),E(append),E(getitem),E(iterate))));}}
  }}
 }
 else if(name=="normalize_vertices_and_edges"){
  result=tuple({E(array(type("float32"),2,"C")),E(array(type("int64"),2,"C"))});nb::object argument=J(arguments,ZERO),array_type=type("Array");int is_array=PyObject_IsInstance(argument.ptr(),array_type.ptr());if(is_array<0)throw nb::python_error();if(is_array&&nb::cast<int>(ac_attr(argument,"ndim"))==2&&ac_call(type("unliteral"),{E(J(arguments,ONE))}).ptr()==type("boolean").ptr()&&nb::cast<int>(ac_attr(type("intp"),"bitwidth"))==64){nb::object input=ac_attr(argument,"dtype");const int width=input.ptr()==type("float32").ptr()?4:input.ptr()==type("float64").ptr()?8:0;if(width){nb::object numpy=ns.get("np"),ndarray=ac_attr(numpy,"ndarray"),empty=ac_attr(numpy,"empty"),numpy_array=ac_attr(numpy,"array"),key=ac_call(type("UniTuple"),{E(input),E(TWO)}),edge=ac_call(type("UniTuple"),{E(type("int64")),E(TWO)}),dictionary=ac_call(type("DictType"),{E(key),E(type("int64"))}),set=ac_call(type("Set"),{E(edge)}),row=ac_call(ac_attr(argument,"copy"),{},{{"ndim",E(ONE)},{"layout",[]{return nb::str("A");}}}),list=ac_call(type("List"),{E(row)});if(PyType_Check(ndarray.ptr())&&PyCFunction_Check(empty.ptr())&&PyCFunction_Check(numpy_array.ptr()))compile_metadata=T(E(nb::str(ac_normalize_vertices_plan_tag)),E(arguments),E(T(E(nb::int_(width)),E(nb::int_(4)),E(nb::int_(8)))),E(ndarray),E(T(E(empty),E(numpy_array),E(ac_attr(numpy,"float32")),E(ac_attr(numpy,"int64")))),E(T(E(dictionary),E(list),E(set))),E(nb::int_(64)));}}
 }
 else throw nb::type_error("unsupported native Numba acceleration signature");
 return T(E(result),E(compile_metadata));
}
ShapeAccelerationLocals shape_accel_numba_locals(nb::object operation,nb::tuple arguments,nb::tuple layout){
 const auto& entry=ac_entry(nb::cast<std::string>(operation));if(arguments.size()!=entry.parameters.size())throw nb::type_error("native Numba acceleration argument count differs from source");
 nb::dict bindings;for(size_t i=0;i<entry.parameters.size();++i)bindings[entry.parameters[i]]=arguments[i];
 return shape_accel_locals(std::move(layout),std::move(bindings),nb::dict());
}
nb::object shape_accel_numba_call(nb::object operation,nb::tuple literals,nb::dict globals,ShapeAccelerationLocals& owner,nb::object compile_types){
 const auto& entry=ac_entry(nb::cast<std::string>(operation));
 if(!compile_types.is_none()){
  AccelerationReconstructPlan reconstruct_plan;if(ac_reconstruct_plan_decode(compile_types,reconstruct_plan)){if(std::strcmp(entry.name,"reconstruct_polygons_from_edges")!=0)throw nb::value_error("reconstruction plan used for a different native operation");nb::object result;if(ac_reconstruct_fast(reconstruct_plan,owner,result))return result;compile_types=std::move(reconstruct_plan.arguments);}
 }
 if(!compile_types.is_none()){
  AccelerationDedupPlan dedup_plan;if(ac_dedup_plan_decode(compile_types,dedup_plan)){if(std::strcmp(entry.name,"remove_path_duplicates")!=0)throw nb::value_error("dedup plan used for a different native operation");nb::object result;if(ac_dedup_fast(dedup_plan,owner,result))return result;compile_types=std::move(dedup_plan.arguments);}
 }
 if(!compile_types.is_none()){
  AccelerationNormalizeVerticesPlan normalize_plan;if(ac_normalize_vertices_plan_decode(compile_types,normalize_plan)){if(std::strcmp(entry.name,"normalize_vertices_and_edges")!=0)throw nb::value_error("vertex normalization plan used for a different native operation");nb::object result;if(ac_normalize_vertices_fast(normalize_plan,owner,result))return result;compile_types=std::move(normalize_plan.arguments);}
 }
 if(!compile_types.is_none()){
  AccelerationConvexPlan convex_plan;
  if(ac_convex_plan_decode(compile_types,convex_plan)){
   if(std::strcmp(entry.name,"is_convex")!=0)throw nb::value_error("convex plan used for a different native operation");
   nb::object result;if(ac_convex_fast(convex_plan,owner,result))return result;
   compile_types=std::move(convex_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationEdgeOwnerPlan edge_owner_plan;
  if(ac_edge_owner_plan_decode(compile_types,edge_owner_plan)){
   if(std::strcmp(entry.name,"generate_2D_edge_meshes")!=0)throw nb::value_error("edge-owner plan used for a different native operation");
   nb::object result;if(ac_edge_owner_fast(edge_owner_plan,globals,owner,result))return result;
   compile_types=std::move(edge_owner_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationBoundingBoxPlan bounding_box_plan;
  if(ac_bounding_box_plan_decode(compile_types,bounding_box_plan)){
   if(std::strcmp(entry.name,"create_box_from_bounding")!=0)throw nb::value_error("bounding-box plan used for a different native operation");
   nb::object result;if(ac_bounding_box_fast(bounding_box_plan,owner,result))return result;
   compile_types=std::move(bounding_box_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationPolarAnglesPlan polar_plan;
  if(ac_polar_angles_plan_decode(compile_types,polar_plan)){
   if(std::strcmp(entry.name,"_are_polar_angles_monotonic")!=0)throw nb::value_error("polar-angle plan used for a different native operation");
   nb::object result;if(ac_polar_angles_fast(polar_plan,owner,result))return result;
   compile_types=std::move(polar_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationOutputSizePlan size_plan;
  if(ac_output_size_plan_decode(compile_types,size_plan)){
   if(std::strcmp(entry.name,"_calc_output_size")!=0)throw nb::value_error("output-size plan used for a different native operation");
   nb::object result;if(ac_output_size_fast(size_plan,owner,result))return result;
   compile_types=std::move(size_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationEdgeLoopPlan edge_loop_plan;
  if(ac_edge_loop_plan_decode(compile_types,edge_loop_plan)){
   if(std::strcmp(entry.name,"_generate_2D_edge_meshes_loop")!=0)throw nb::value_error("edge loop plan used for a different native operation");
   nb::object result;if(ac_edge_loop_fast(edge_loop_plan,owner,result))return result;
   compile_types=std::move(edge_loop_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationNormalizeTrianglesPlan normalize_plan;
  if(ac_normalize_triangles_plan_decode(compile_types,normalize_plan)){
   if(std::strcmp(entry.name,"_normalize_triangle_orientation")!=0)throw nb::value_error("triangle normalization plan used for a different native operation");
   nb::object result;if(ac_normalize_triangles_fast(normalize_plan,owner,result))return result;
   compile_types=std::move(normalize_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationDirectionsFloatPlan directions_plan;
  if(ac_directions_float_plan_decode(compile_types,directions_plan)){
   if(std::strcmp(entry.name,"_direction_vec_and_half_length")!=0)throw nb::value_error("directions typing plan used for a different native operation");
   nb::object result;if(ac_directions_float_fast(directions_plan,owner,result))return result;
   compile_types=std::move(directions_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationCentersFloatPlan centers_plan;
  if(ac_centers_float_plan_decode(compile_types,centers_plan)){
   if(std::strcmp(entry.name,"_set_centers_and_offsets")!=0)throw nb::value_error("centers typing plan used for a different native operation");
   nb::object result;if(ac_centers_float_fast(centers_plan,owner,result))return result;
   compile_types=std::move(centers_plan.arguments);
  }
 }
 if(!compile_types.is_none()){
  AccelerationOrientationFloatPlan orientation_plan;
  if(ac_orientation_float_plan_decode(compile_types,orientation_plan)){
   if(std::strcmp(entry.name,"_orientation")!=0)throw nb::value_error("orientation typing plan used for a different native operation");
   nb::object result;if(ac_orientation_float_fast(orientation_plan,owner,result))return result;
   compile_types=std::move(orientation_plan.arguments);
  }else{
   AccelerationScalarFloatPlan scalar_plan;
   if(ac_scalar_float_plan_decode(compile_types,scalar_plan)){
    if(std::strcmp(entry.name,"_dot")!=0&&std::strcmp(entry.name,"_cross_z")!=0)throw nb::value_error("scalar typing plan used for a different native operation");
    nb::object result;if(ac_scalar_float_fast(entry.name,scalar_plan,owner,result))return result;
    // Unsupported runtime storage/classes/shapes keep complete native behavior.
    compile_types=std::move(scalar_plan.arguments);
   }
  }
 }
 struct ScopedTyping{AccelerationTyping* previous;explicit ScopedTyping(AccelerationTyping* next):previous(ac_active_typing){ac_active_typing=next;}~ScopedTyping(){ac_active_typing=previous;}};
 AccelerationTyping metadata;
 if(!compile_types.is_none()){
  AccelerationGlobals ns(globals);metadata.context=ac_attr(ns.get("_shape_numba_target"),"typing_context");metadata.operators=ns.get("_shape_numba_operator");metadata.numpy=ns.get("np");metadata.as_dtype=ns.get("_shape_numba_as_dtype");metadata.argument_types=compile_types;metadata.argument_value=[&](size_t index){return owner.view(entry.parameters[index]);};
 }
 ScopedTyping active(compile_types.is_none()?nullptr:&metadata);
 return entry.engine(std::move(globals),std::move(literals),owner);
}
nb::object shape_accel_numba_install_py_func(nb::object dispatcher,nb::object public_function){
 // With DISABLE_JIT, genuine njit returns the undecorated Python function.
 const bool disabled=PyFunction_Check(dispatcher.ptr())!=0;
 nb::object original=disabled?dispatcher:ac_attr(dispatcher,"py_func");
 for(const char* name:{"__name__","__qualname__","__module__","__doc__","__annotations__","__defaults__","__kwdefaults__"}){
  nb::object value=ac_attr(original,name);if(PyObject_SetAttrString(public_function.ptr(),name,value.ptr())<0)throw nb::python_error();
 }
 if(disabled)return public_function;
 if(PyObject_SetAttrString(dispatcher.ptr(),"py_func",public_function.ptr())<0||PyObject_SetAttrString(dispatcher.ptr(),"__wrapped__",public_function.ptr())<0)throw nb::python_error();
 return dispatcher;
}

nb::dict shape_accel_namespace(){PyObject* values=PyEval_GetGlobals();if(!values){PyErr_SetString(PyExc_RuntimeError,"executing acceleration namespace is unavailable");throw nb::python_error();}return nb::borrow<nb::dict>(values);}
nb::object shape_accel_normal_adapter_factory(nb::object original,nb::dict globals,nb::object call_factory){
 AccelerationGlobals ns(std::move(globals));nb::object decorator=FN("wraps",E(original));original.reset();nb::object call=ac_call(std::move(call_factory));
 // Genuine declaration metadata is restored before the source decorator sees it.
 if(PyObject_SetAttrString(call.ptr(),"__qualname__",nb::str("normal_adapter.<locals>.call").ptr())<0)throw nb::python_error();
 return ac_call(std::move(decorator),{E(std::move(call))});
}
nb::object shape_accel_normal_adapter_call(nb::object original_getter,nb::dict globals,nb::tuple literals,ShapeAccelerationLocals& o){BEGIN;
 nb::object first=NP("asarray",E(O(a))),second=NP("asarray",E(O(b))),reference=NP("asarray",E(O(p)));
 P(first,std::move(first));P(second,std::move(second));P(reference,std::move(reference));
 auto member=[&](nb::object value,nb::object container){int result=PySequence_Contains(container.ptr(),value.ptr());if(result<0)throw nb::python_error();return result!=0;};
 nb::object dimensions=nb::borrow<nb::object>(PyTuple_GetItem(literals.ptr(),1));if(!dimensions.is_valid())throw nb::python_error();
 bool unsupported=!member(ac_attr(O(first),"ndim"),dimensions);
 if(!unsupported)unsupported=ac_truth(C(ac_attr(O(first),"shape"),ac_attr(O(second),"shape"),Py_NE));
 if(!unsupported){nb::object shape=ac_attr(O(first),"shape");nb::object last=J(shape,nb::int_(-1));nb::object allowed=nb::borrow<nb::object>(PyTuple_GetItem(literals.ptr(),3));if(!allowed.is_valid())throw nb::python_error();unsupported=!member(last,allowed);}
 if(!unsupported)unsupported=ac_truth(C(ac_attr(O(first),"dtype"),ac_attr(ns.get("np"),"float64"),Py_NE));
 if(!unsupported)unsupported=ac_truth(C(ac_attr(O(second),"dtype"),ac_attr(ns.get("np"),"float64"),Py_NE));
 if(!unsupported){nb::object required=nb::borrow<nb::object>(PyTuple_GetItem(literals.ptr(),4));if(!required.is_valid())throw nb::python_error();unsupported=ac_truth(C(ac_attr(O(reference),"shape"),required,Py_NE));}
 if(!unsupported)unsupported=!member(ac_attr(O(reference),"dtype"),ns.get("_REAL_DTYPES"));
 if(unsupported)return ac_call(ac_call(std::move(original_getter)),{E(O(a)),E(O(b)),E(O(p))});
 return ac_call(ac_attr(ns.get("_native"),"segment_normal"),{E(O(first)),E(O(second)),E(FN("_double",E(O(reference))))});
}
} // anonymous namespace

void bind_shape_acceleration(nb::module_& m){
 auto owner=nb::class_<ShapeAccelerationLocals>(m,"ShapeAccelerationLocals",nb::is_weak_referenceable(),nb::type_slots(ac_owner_slots));
 const char* fields[]={
 "a","b","c","p","data","closed","diff","dup","indices","x","y","path","limit","bevel","_ext_point1","_ext_point2","full_path","full_normals","miters","mf_dot","miter_lengths_squared","miter_signs","centers","offsets","triangles0","triangles","idx_bevel","idx_offset","idx_bevel_outside","idx_bevel_inside","sign_bevel","_nonspecial","centers_bevel","offsets_bevel","n_centers","triangles_bevel","flip_idx","bounding_box","tl","br","tr","bl","vertices","edges","adjacency","edge_","v1","v2","unvisited_edges","polygons","edge","current_vertex","start_vertex","polygon_indices","next_vertex","neighbor","polygon_vertices","close","vertex_to_idx","new_vertices","prev_idx","i","vertex","vertex_t","current_idx","idx","new_vertices_array","edges_array","poly","orientation_","centroid","angles","shifted_angles","q","r","val","fst","snd","thrd","orn_set","v0","v1","f","v","ccw","direction_vectors","cos_miter_limit","n","point_count","bevel_count","start","stop","cos_angle","vertex","vec1","vec2","half_vec1_len","half_vec2_len","j","cos_limit","always_bevel","sin_angle","miter","scale_factor","sign","mag","clockwise","counterclockwise","invert","ti","p1","p2","p3","start_angle","prev_angle","angle","n_points","orientation_set","current_orientation","idx0","idx1","idx2","triangle_orientation","normals","vec_len_arr","vec_diff","bevel_limit_array","path_","dup_count","target_len","new_path","index","x_min","x_max","y_min","y_max","result","n_edges","visited","n_vertices","incident","_","end","start_v","current_v","found","edge_idx","next_v","polygon","d","normal","norm","ind","first","second","reference"};
 std::vector<std::string> seen;for(const char* name:fields){if(std::find(seen.begin(),seen.end(),name)!=seen.end())continue;seen.emplace_back(name);owner.def_prop_ro(name,[name](const ShapeAccelerationLocals& value){if(std::find(value.names.begin(),value.names.end(),name)==value.names.end())return nb::object(nb::none());return value.view(name);});}
 nb::class_<ShapeAccelerationIterator>(m,"ShapeAccelerationIterator",nb::is_weak_referenceable(),nb::type_slots(ac_iterator_slots))
  .def("__iter__",[](nb::object self){return self;})
  .def("__next__",[](ShapeAccelerationIterator& state){return state.send(nb::none());})
  .def("send",&ShapeAccelerationIterator::send,nb::arg("value").none())
  .def("throw",&ShapeAccelerationIterator::throw_)
  .def("close",&ShapeAccelerationIterator::close);
 m.def("shape_accel_locals",&shape_accel_locals,nb::arg("layout"),nb::arg("bindings"),nb::arg("cells"));
#define AC_BIND(name) m.def(#name,&name,nb::arg("namespace"),nb::arg("literals"),nb::arg("owner"));
 AC_BIND(shape_accel_remove_duplicates_py)
 AC_BIND(shape_accel_mirror)
 AC_BIND(shape_accel_sign_nonzero)
 AC_BIND(shape_accel_sign_cross)
 AC_BIND(shape_accel_box_py)
 AC_BIND(shape_accel_reconstruct_py)
 AC_BIND(shape_accel_normalize_py)
 AC_BIND(shape_accel_monotonic_py)
 AC_BIND(shape_accel_orientation_py)
 AC_BIND(shape_accel_vectorized_orientation)
 AC_BIND(shape_accel_cross2d)
 AC_BIND(shape_accel_common_orientation)
 AC_BIND(shape_accel_convex_py)
 AC_BIND(shape_accel_segment_normal)
 AC_BIND(shape_accel_numba_dot)
 AC_BIND(shape_accel_numba_cross)
 AC_BIND(shape_accel_numba_sign_abs)
 AC_BIND(shape_accel_numba_orthogonal)
 AC_BIND(shape_accel_numba_output_size)
 AC_BIND(shape_accel_numba_set_centers)
 AC_BIND(shape_accel_numba_normalize_triangles)
 AC_BIND(shape_accel_numba_orientation)
 AC_BIND(shape_accel_numba_monotonic)
 AC_BIND(shape_accel_numba_convex)
 AC_BIND(shape_accel_numba_directions)
 AC_BIND(shape_accel_numba_edges_loop)
 AC_BIND(shape_accel_numba_cut_end)
 AC_BIND(shape_accel_numba_edges)
 AC_BIND(shape_accel_numba_remove_duplicates)
 AC_BIND(shape_accel_numba_box)
 AC_BIND(shape_accel_numba_reconstruct)
 AC_BIND(shape_accel_numba_normalize)
#undef AC_BIND
 m.def("shape_accel_edges_py",&shape_accel_edges_py,nb::arg("namespace"),nb::arg("literals"),nb::arg("owner"),nb::arg("generator_factory").none());
 m.def("shape_accel_numba_signature",&shape_accel_numba_signature,nb::arg("operation").none(),nb::arg("argument_types").none(),nb::arg("namespace"));
 m.def("shape_accel_numba_locals",&shape_accel_numba_locals,nb::arg("operation").none(),nb::arg("arguments"),nb::arg("layout"));
 m.def("shape_accel_numba_call",&shape_accel_numba_call,nb::arg("operation").none(),nb::arg("literals"),nb::arg("namespace"),nb::arg("owner"),nb::arg("compile_types").none());
 m.def("shape_accel_numba_install_py_func",&shape_accel_numba_install_py_func,nb::arg("dispatcher").none(),nb::arg("public_function").none());
 m.def("shape_accel_normal_adapter_factory",&shape_accel_normal_adapter_factory,nb::arg("original").none(),nb::arg("namespace"),nb::arg("call_factory").none());
 m.def("shape_accel_normal_adapter_call",&shape_accel_normal_adapter_call,nb::arg("original_getter").none(),nb::arg("namespace"),nb::arg("literals"),nb::arg("owner"));
 m.def("shape_accel_namespace",&shape_accel_namespace);
}
} // namespace nx
