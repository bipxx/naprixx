#include "arrays.hpp"
#include "collection_protocol.hpp"
#include <array>
#include <functional>
#include <initializer_list>
#include <utility>

namespace nx {
namespace {
nb::object vd_check(PyObject* value) { if (!value) throw nb::python_error(); return nb::steal<nb::object>(value); }
nb::object vd_attr(nb::object value, const char* name) { return vd_check(PyObject_GetAttrString(value.ptr(), name)); }
nb::object vd_item(nb::object value, nb::object index) { return vd_check(PyObject_GetItem(value.ptr(), index.ptr())); }
bool vd_truth(nb::object value) { int answer=PyObject_IsTrue(value.ptr()); if(answer<0)throw nb::python_error(); return answer!=0; }
nb::object vd_iter(nb::object value) { return vd_check(PyObject_GetIter(value.ptr())); }
nb::object vd_next(nb::handle iterator) { PyObject* value=PyIter_Next(iterator.ptr()); if(!value&&PyErr_Occurred())throw nb::python_error(); return nb::steal<nb::object>(value); }
void vd_assign(nb::object& target, nb::object value) { nb::object previous=std::move(target);target=std::move(value);previous.reset(); }
using VDExpr=std::function<nb::object()>;
using VDKw=std::pair<const char*,VDExpr>;
nb::object vd_call(nb::object function,std::initializer_list<VDExpr> args={},std::initializer_list<VDKw> keywords={}) {
 nb::tuple values=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(args.size())));if(!values.is_valid())throw nb::python_error();Py_ssize_t i=0;for(const auto& expression:args){nb::object value=expression();PyTuple_SET_ITEM(values.ptr(),i++,value.release().ptr());}
 nb::dict kwargs;for(const auto& keyword:keywords)kwargs[keyword.first]=keyword.second();return vd_check(PyObject_Call(function.ptr(),values.ptr(),keywords.size()?kwargs.ptr():nullptr));
}
nb::tuple vd_tuple(std::initializer_list<VDExpr> expressions) { nb::tuple result=nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(expressions.size())));if(!result.is_valid())throw nb::python_error();Py_ssize_t i=0;for(const auto& expression:expressions){nb::object value=expression();PyTuple_SET_ITEM(result.ptr(),i++,value.release().ptr());}return result; }
nb::list vd_list(std::initializer_list<VDExpr> expressions) { nb::list result;for(const auto& expression:expressions)result.append(expression());return result; }
nb::object vd_binary(VDExpr left,VDExpr right,binaryfunc operation) { nb::object a=left(),b=right();return vd_check(operation(a.ptr(),b.ptr())); }
nb::object vd_compare(VDExpr left,VDExpr right,int operation) { nb::object a=left(),b=right();return vd_check(PyObject_RichCompare(a.ptr(),b.ptr(),operation)); }
nb::object vd_subscript(VDExpr value,VDExpr index) { nb::object receiver=value(),key=index();return vd_item(std::move(receiver),std::move(key)); }
void vd_publish(VDExpr value,VDExpr receiver,const char* name) { nb::object result=value(),target=receiver();if(PyObject_SetAttrString(target.ptr(),name,result.ptr())<0)throw nb::python_error();target.reset();result.reset(); }
void vd_set_item(VDExpr value,VDExpr receiver,VDExpr index) { nb::object result=value(),target=receiver(),key=index();if(PyObject_SetItem(target.ptr(),key.ptr(),result.ptr())<0)throw nb::python_error();key.reset();target.reset();result.reset(); }
nb::object vd_slice(nb::object start,nb::object stop) { return vd_check(PySlice_New(start.ptr(),stop.ptr(),Py_None)); }
#define VX(value) [&]() -> nb::object { return (value); }
#define VA(left,right) vd_binary(VX(left),VX(right),PyNumber_Add)
#define VS(left,right) vd_binary(VX(left),VX(right),PyNumber_Subtract)
#define VM(left,right) vd_binary(VX(left),VX(right),PyNumber_Multiply)
#define VC(left,right,op) vd_compare(VX(left),VX(right),op)
#define VI(value,index) vd_subscript(VX(value),VX(index))
#define VP(value,receiver,name) vd_publish(VX(value),VX(receiver),name)
struct ViewerDimsGlobals {
 nb::dict values;nb::object builtins;
 explicit ViewerDimsGlobals(nb::dict source):values(std::move(source)),builtins(nb::borrow<nb::object>(PyEval_GetBuiltins())) {}
 ViewerDimsGlobals(nb::dict source,nb::object builtin_source):values(std::move(source)),builtins(std::move(builtin_source)) {}
 nb::object get(const char* name)const {
  nb::object key=nb::str(name);PyObject* value=PyDict_GetItemWithError(values.ptr(),key.ptr());if(value)return nb::borrow<nb::object>(value);if(PyErr_Occurred())throw nb::python_error();value=PyObject_GetItem(builtins.ptr(),key.ptr());if(value)return nb::steal<nb::object>(value);if(!PyErr_ExceptionMatches(PyExc_KeyError))throw nb::python_error();PyErr_Clear();
  nb::object message=vd_check(PyUnicode_FromFormat("name '%s' is not defined",name)),error=vd_call(nb::borrow<nb::object>(PyExc_NameError),{VX(message)});if(PyObject_SetAttrString(error.ptr(),"name",key.ptr())<0)throw nb::python_error();PyErr_SetObject(PyExc_NameError,error.ptr());throw nb::python_error();
 }
};
// These are actual assigned source locals, not Viewer/Layer model state.
struct ViewerDimsReconciliationLocals {
 nb::object layers,layer,position,ind,updated_axis_labels,pos,label,ranges,layers_are_default,new_dim,dim_diff,prev,default_labels;
 ViewerDimsReconciliationLocals()=default;ViewerDimsReconciliationLocals(ViewerDimsReconciliationLocals&&)=default;
 void clear(){std::array<nb::object,13> values={std::move(layers),std::move(layer),std::move(position),std::move(ind),std::move(updated_axis_labels),std::move(pos),std::move(label),std::move(ranges),std::move(layers_are_default),std::move(new_dim),std::move(dim_diff),std::move(prev),std::move(default_labels)};for(auto& value:values)value.reset();}
 ~ViewerDimsReconciliationLocals(){clear();}
};
int vd_owner_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ViewerDimsReconciliationLocals>(self);for(nb::object* value:{&o.layers,&o.layer,&o.position,&o.ind,&o.updated_axis_labels,&o.pos,&o.label,&o.ranges,&o.layers_are_default,&o.new_dim,&o.dim_diff,&o.prev,&o.default_labels})Py_VISIT(value->ptr());}return 0;}
int vd_owner_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ViewerDimsReconciliationLocals>(self)->clear();return 0;}
PyType_Slot vd_owner_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(vd_owner_traverse)},{Py_tp_clear,reinterpret_cast<void*>(vd_owner_clear)},{0,nullptr}};

// A thin genuine Python yield-from envelope carries this source iterator. As in
// accepted Shape S2, an iteration error closes execution but retains .0/current
// leases while the saved traceback/envelope roots exist. Close/throw on that
// already-closed iterator do not clear the still-observable error leases.
struct ViewerDimsReconciliationIterator {
 nb::object iterator,current,namespace_,builtins;bool stringify=false,started=false,closed=false,running=false;
 ViewerDimsReconciliationIterator(nb::object source,bool strings,ViewerDimsGlobals& ns):iterator(std::move(source)),namespace_(ns.values),builtins(ns.builtins),stringify(strings) {}
 ViewerDimsReconciliationIterator(ViewerDimsReconciliationIterator&&)=default;
 void clear(){nb::object a=std::move(iterator),b=std::move(current),c=std::move(namespace_),d=std::move(builtins);a.reset();b.reset();c.reset();d.reset();}
 ~ViewerDimsReconciliationIterator(){clear();}
 nb::object send(nb::object sent){if(running)throw nb::value_error("generator already executing");if(closed)throw nb::stop_iteration();if(!started&&!sent.is_none())throw nb::type_error("can't send non-None value to a just-started generator");running=true;started=true;
  try{nb::object value=vd_next(iterator);if(!value.is_valid()){closed=true;running=false;clear();throw nb::stop_iteration();}vd_assign(current,std::move(value));ViewerDimsGlobals ns(nb::borrow<nb::dict>(namespace_),builtins);nb::object result=stringify?vd_call(ns.get("str"),{VX(current)}):vd_call(vd_attr(current,"_has_default_axis_labels"));running=false;return result;}
  catch(const nb::python_error& error){running=false;closed=true;if(error.matches(PyExc_StopIteration))collections::raise_exception(error.value(),true);throw;}catch(...){running=false;throw;}
 }
 void close(){if(running)throw nb::value_error("generator already executing");if(closed)return;closed=true;clear();}
 nb::object throw_(nb::args args){if(running)throw nb::value_error("generator already executing");nb::object error=collections::normalize_throw(args);bool was_closed=closed;closed=true;collections::raise_exception(error,!was_closed);}
};
int vd_iterator_traverse(PyObject* self,visitproc visit,void* arg){Py_VISIT(Py_TYPE(self));if(nb::inst_ready(self)){auto& o=*nb::inst_ptr<ViewerDimsReconciliationIterator>(self);Py_VISIT(o.iterator.ptr());Py_VISIT(o.current.ptr());Py_VISIT(o.namespace_.ptr());Py_VISIT(o.builtins.ptr());}return 0;}
int vd_iterator_clear(PyObject* self){if(nb::inst_ready(self))nb::inst_ptr<ViewerDimsReconciliationIterator>(self)->clear();return 0;}
PyType_Slot vd_iterator_slots[]={{Py_tp_traverse,reinterpret_cast<void*>(vd_iterator_traverse)},{Py_tp_clear,reinterpret_cast<void*>(vd_iterator_clear)},{0,nullptr}};
nb::object vd_generator(nb::object source,bool strings,ViewerDimsGlobals& ns,nb::object factory,const char* qualname){nb::object native=nb::cast(ViewerDimsReconciliationIterator(std::move(source),strings,ns));nb::object result=vd_call(std::move(factory),{VX(native)});if(PyObject_SetAttrString(result.ptr(),"__name__",nb::str("<genexpr>").ptr())<0||PyObject_SetAttrString(result.ptr(),"__qualname__",nb::str(qualname).ptr())<0)throw nb::python_error();return result;}
std::array<nb::object,2> vd_unpack_pair(nb::object value){PyObject* raw=PyObject_GetIter(value.ptr());if(!raw){nb::python_error error;if(error.matches(PyExc_TypeError)&&Py_TYPE(value.ptr())->tp_iter==nullptr&&!PySequence_Check(value.ptr())){PyErr_Format(PyExc_TypeError,"cannot unpack non-iterable %.200s object",Py_TYPE(value.ptr())->tp_name);throw nb::python_error();}throw error;}nb::object iterator=nb::steal<nb::object>(raw);std::array<nb::object,2> result;for(size_t i=0;i<2;++i){result[i]=vd_next(iterator);if(!result[i].is_valid()){PyErr_Format(PyExc_ValueError,"not enough values to unpack (expected 2, got %zu)",i);throw nb::python_error();}}nb::object extra=vd_next(iterator);if(extra.is_valid()){PyErr_SetString(PyExc_ValueError,"too many values to unpack (expected 2)");throw nb::python_error();}iterator.reset();value.reset();return result;}

nb::object viewer_dims_sliced_extent(nb::object self,nb::dict namespace_,ViewerDimsReconciliationLocals& o){ViewerDimsGlobals ns(std::move(namespace_));vd_assign(o.layers,!o.layers.is_none()?vd_call(ns.get("LayerList"),{VX(o.layers)}):vd_attr(self,"layers"));nb::object iterator=vd_iter(o.layers);
 while(nb::object layer=vd_next(iterator)){vd_assign(o.layer,std::move(layer));nb::object contained=vd_attr(self,"layers");int present=PySequence_Contains(contained.ptr(),o.layer.ptr());if(present<0)throw nb::python_error();contained.reset();if(!present){nb::object exception=ns.get("ValueError"),formatted;{nb::object name=vd_attr(o.layer,"name");formatted=vd_check(PyObject_Format(name.ptr(),nb::str("").ptr()));}nb::tuple pieces=vd_tuple({VX(nb::str("layer \"")),VX(formatted),VX(nb::str("\" is not part of this viewer."))});nb::object message=vd_check(PyUnicode_Join(nb::str("").ptr(),pieces.ptr()));pieces.reset();formatted.reset();nb::object error=vd_call(std::move(exception),{VX(message)});message.reset();collections::raise_exception(error,false);}}
 iterator.reset();if(vd_truth(VC(vd_call(ns.get("len"),{VX(o.layers)}),nb::int_(0),Py_EQ))){nb::object stack=vd_attr(ns.get("np"),"vstack");return vd_call(std::move(stack),{VX(vd_list({VX(vd_call(vd_attr(ns.get("np"),"full"),{VX(vd_attr(vd_attr(self,"dims"),"ndim")),VX(nb::float_(-0.5))})),VX(vd_call(vd_attr(ns.get("np"),"full"),{VX(vd_attr(vd_attr(self,"dims"),"ndim")),VX(nb::float_(511.5))}))}))});}
 return VI(vd_attr(o.layers,"_extent_world_augmented"),vd_tuple({VX(vd_slice(nb::none(),nb::none())),VX(vd_attr(vd_attr(self,"dims"),"displayed"))}));}
void viewer_dims_on_layer_reload(nb::object self,nb::object event,nb::dict){VP(vd_attr(vd_attr(vd_attr(self,"layers"),"extent"),"units"),vd_attr(self,"dims"),"units");nb::object submit=vd_attr(vd_attr(self,"_layer_slicer"),"submit");vd_call(std::move(submit),{},{{"layers",VX(vd_list({VX(vd_attr(event,"layer"))}))},{"dims",VX(vd_attr(self,"dims"))},{"force",VX(nb::bool_(true))}});}
void viewer_dims_update_layers(nb::object self,nb::dict namespace_,ViewerDimsReconciliationLocals& o){ViewerDimsGlobals ns(std::move(namespace_));vd_assign(o.layers,vd_truth(o.layers)?o.layers:vd_attr(self,"layers"));VP(vd_attr(vd_attr(vd_attr(self,"layers"),"extent"),"units"),vd_attr(self,"dims"),"units");nb::object submit=vd_attr(vd_attr(self,"_layer_slicer"),"submit");vd_call(std::move(submit),{},{{"layers",VX(o.layers)},{"dims",VX(vd_attr(self,"dims"))}});vd_assign(o.position,vd_call(ns.get("list"),{VX(vd_attr(vd_attr(self,"cursor"),"position"))}));if(vd_truth(VC(vd_call(ns.get("len"),{VX(o.position)}),vd_attr(vd_attr(self,"dims"),"ndim"),Py_LT)))vd_assign(o.position,VM(vd_list({VX(nb::float_(0.0))}),vd_attr(vd_attr(self,"dims"),"ndim")));
 nb::object iterator=vd_iter(VI(vd_attr(vd_attr(self,"dims"),"order"),vd_slice(nb::none(),vd_check(PyNumber_Negative(vd_attr(vd_attr(self,"dims"),"ndisplay").ptr())))));while(nb::object ind=vd_next(iterator)){vd_assign(o.ind,std::move(ind));vd_set_item(VX(VI(vd_attr(vd_attr(self,"dims"),"point"),o.ind)),VX(o.position),VX(o.ind));}iterator.reset();VP(vd_call(ns.get("tuple"),{VX(o.position)}),vd_attr(self,"cursor"),"position");}
nb::object viewer_dims_merge_axis_labels(nb::object self,nb::dict namespace_,ViewerDimsReconciliationLocals& o){ViewerDimsGlobals ns(std::move(namespace_));vd_assign(o.updated_axis_labels,vd_call(ns.get("list"),{VX(vd_attr(vd_attr(self,"dims"),"axis_labels"))}));nb::object enumeration=vd_call(ns.get("enumerate"),{VX(vd_attr(vd_attr(self,"layers"),"axis_labels"))}),iterator=vd_iter(std::move(enumeration));while(nb::object pair=vd_next(iterator)){auto parts=vd_unpack_pair(std::move(pair));vd_assign(o.pos,std::move(parts[0]));vd_assign(o.label,std::move(parts[1]));if(vd_truth(VC(o.label,vd_call(ns.get("str"),{VX(VS(o.pos,vd_attr(vd_attr(self,"dims"),"ndim")))}),Py_NE)))vd_set_item(VX(o.label),VX(o.updated_axis_labels),VX(o.pos));}iterator.reset();return vd_call(ns.get("tuple"),{VX(o.updated_axis_labels)});}
void viewer_dims_on_layers_change(nb::object self,nb::dict namespace_,ViewerDimsReconciliationLocals& o,nb::object factory){ViewerDimsGlobals ns(std::move(namespace_));if(vd_truth(VC(vd_call(ns.get("len"),{VX(vd_attr(self,"layers"))}),nb::int_(0),Py_EQ))){VP(nb::int_(2),vd_attr(self,"dims"),"ndim");vd_call(vd_attr(vd_attr(self,"dims"),"reset"));VP(nb::bool_(false),self,"_layers_had_custom_axis_labels");}else{vd_assign(o.ranges,vd_attr(vd_attr(self,"layers"),"_ranges"));VP(vd_call(ns.get("len"),{VX(o.ranges)}),vd_attr(self,"dims"),"ndim");VP(o.ranges,vd_attr(self,"dims"),"range");VP(vd_attr(vd_attr(self,"layers"),"units"),vd_attr(self,"dims"),"units");nb::object all=ns.get("all"),iterator=vd_iter(vd_attr(self,"layers")),generator=vd_generator(std::move(iterator),false,ns,std::move(factory),"ViewerModel._on_layers_change.<locals>.<genexpr>");nb::object value=vd_call(std::move(all),{VX(generator)});generator.reset();vd_assign(o.layers_are_default,std::move(value));
  if(vd_truth(vd_attr(self,"_layers_had_custom_axis_labels"))&&vd_truth(o.layers_are_default))VP(vd_attr(vd_attr(self,"layers"),"axis_labels"),vd_attr(self,"dims"),"axis_labels");else VP(vd_call(vd_attr(self,"_merge_dims_and_layers_axis_labels")),vd_attr(self,"dims"),"axis_labels");VP(nb::bool_(!vd_truth(o.layers_are_default)),self,"_layers_had_custom_axis_labels");}
 vd_assign(o.new_dim,vd_attr(vd_attr(self,"dims"),"ndim"));vd_assign(o.dim_diff,VS(o.new_dim,vd_call(ns.get("len"),{VX(vd_attr(vd_attr(self,"cursor"),"position"))})));if(vd_truth(VC(o.dim_diff,nb::int_(0),Py_LT)))VP(VI(vd_attr(vd_attr(self,"cursor"),"position"),vd_slice(nb::none(),o.new_dim)),vd_attr(self,"cursor"),"position");else if(vd_truth(VC(o.dim_diff,nb::int_(0),Py_GT)))VP(vd_call(ns.get("tuple"),{VX(VA(vd_call(ns.get("list"),{VX(vd_attr(vd_attr(self,"cursor"),"position"))}),VM(vd_list({VX(nb::int_(0))}),o.dim_diff)))}),vd_attr(self,"cursor"),"position");}
nb::object layer_dims_axis_labels_get(nb::object self){return vd_attr(VI(vd_attr(self,"_transforms"),nb::str("data2physical")),"axis_labels");}
void layer_dims_axis_labels_set(nb::object self,nb::object axis_labels,ViewerDimsReconciliationLocals& o){vd_assign(o.prev,vd_attr(VI(vd_attr(self,"_transforms"),nb::str("data2physical")),"axis_labels"));VP(axis_labels,VI(vd_attr(self,"_transforms"),nb::str("data2physical")),"axis_labels");if(vd_truth(VC(vd_attr(VI(vd_attr(self,"_transforms"),nb::str("data2physical")),"axis_labels"),o.prev,Py_NE)))vd_call(vd_attr(vd_attr(self,"events"),"axis_labels"));}
nb::object layer_dims_has_default_axis_labels(nb::object self,nb::dict namespace_,ViewerDimsReconciliationLocals& o,nb::object factory){ViewerDimsGlobals ns(std::move(namespace_));nb::object tuple=ns.get("tuple"),range=vd_call(ns.get("range"),{VX(vd_check(PyNumber_Negative(vd_attr(self,"ndim").ptr()))),VX(nb::int_(0))}),iterator=vd_iter(std::move(range)),generator=vd_generator(std::move(iterator),true,ns,std::move(factory),"Layer._has_default_axis_labels.<locals>.<genexpr>");nb::object value=vd_call(std::move(tuple),{VX(generator)});generator.reset();vd_assign(o.default_labels,std::move(value));return VC(vd_attr(self,"axis_labels"),o.default_labels,Py_EQ);}
#undef VX
#undef VA
#undef VS
#undef VM
#undef VC
#undef VI
#undef VP
} // namespace
void bind_viewer_dims_reconciliation(nb::module_& m){
 auto owner=nb::class_<ViewerDimsReconciliationLocals>(m,"ViewerDimsReconciliationLocals",nb::is_weak_referenceable(),nb::type_slots(vd_owner_slots));
#define VD_FIELD(name) owner.def_prop_ro(#name,[](const ViewerDimsReconciliationLocals& o){return o.name.is_valid()?o.name:nb::none();});
 VD_FIELD(layers) VD_FIELD(layer) VD_FIELD(position) VD_FIELD(ind) VD_FIELD(updated_axis_labels) VD_FIELD(pos) VD_FIELD(label) VD_FIELD(ranges) VD_FIELD(layers_are_default) VD_FIELD(new_dim) VD_FIELD(dim_diff) VD_FIELD(prev) VD_FIELD(default_labels)
#undef VD_FIELD
 nb::class_<ViewerDimsReconciliationIterator>(m,"ViewerDimsReconciliationIterator",nb::is_weak_referenceable(),nb::type_slots(vd_iterator_slots))
  .def("__iter__",[](nb::object self){return self;}).def("__next__",[](ViewerDimsReconciliationIterator& o){return o.send(nb::none());}).def("send",&ViewerDimsReconciliationIterator::send,nb::arg("value").none()).def("throw",&ViewerDimsReconciliationIterator::throw_).def("close",&ViewerDimsReconciliationIterator::close)
  .def_prop_ro("iterator",[](const ViewerDimsReconciliationIterator& o){return o.iterator.is_valid()?o.iterator:nb::none();}).def_prop_ro("layer",[](const ViewerDimsReconciliationIterator& o){return !o.stringify&&o.current.is_valid()?o.current:nb::none();}).def_prop_ro("i",[](const ViewerDimsReconciliationIterator& o){return o.stringify&&o.current.is_valid()?o.current:nb::none();});
 m.def("viewer_dims_reconciliation_locals",[](nb::object layers){ViewerDimsReconciliationLocals owner;owner.layers=std::move(layers);return owner;},nb::arg("layers").none()=nb::none());
 m.def("viewer_dims_sliced_extent",&viewer_dims_sliced_extent,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
 m.def("viewer_dims_on_layer_reload",&viewer_dims_on_layer_reload,nb::arg("self").none(),nb::arg("event").none(),nb::arg("namespace"));
 m.def("viewer_dims_update_layers",&viewer_dims_update_layers,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
 m.def("viewer_dims_merge_axis_labels",&viewer_dims_merge_axis_labels,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"));
 m.def("viewer_dims_on_layers_change",&viewer_dims_on_layers_change,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("generator_factory").none());
 m.def("layer_dims_axis_labels_get",&layer_dims_axis_labels_get,nb::arg("self").none());
 m.def("layer_dims_axis_labels_set",&layer_dims_axis_labels_set,nb::arg("self").none(),nb::arg("axis_labels").none(),nb::arg("locals_owner"));
 m.def("layer_dims_has_default_axis_labels",&layer_dims_has_default_axis_labels,nb::arg("self").none(),nb::arg("namespace"),nb::arg("locals_owner"),nb::arg("generator_factory").none());
}
} // namespace nx
