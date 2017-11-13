#include "dxbc_gen_common.h"
#include "dxbc_gen_vertex.h"

#include "../dxbc_names.h"

namespace dxvk {
  
  DxbcCodeGen::DxbcCodeGen() {
    m_module.enableCapability(spv::CapabilityShader);
    m_module.setMemoryModel(
      spv::AddressingModelLogical,
      spv::MemoryModelGLSL450);
    m_entryPointId = m_module.allocateId();
  }
  
  
  DxbcCodeGen::~DxbcCodeGen() {
    
  }
  
  
  void DxbcCodeGen::dclTemps(uint32_t n) {
    const uint32_t oldSize = m_rRegs.size();
    
    if (n > oldSize) {
      m_rRegs.resize(n);
      
      DxbcPointer reg;
      reg.type = DxbcPointerType(
        DxbcValueType(DxbcScalarType::Float32, 4),
        spv::StorageClassPrivate);
      
      const uint32_t typeId = this->defPointerType(reg.type);
      
      for (uint32_t i = oldSize; i < n; i++) {
        reg.valueId = m_module.newVar(typeId, spv::StorageClassPrivate);
        m_module.setDebugName(reg.valueId, str::format("r", i).c_str());
        m_rRegs.at(i) = reg;
      }
    }
  }
  
  
  DxbcPointer DxbcCodeGen::ptrTempReg(uint32_t regId) {
    return m_rRegs.at(regId);
  }
  
  
  DxbcValue DxbcCodeGen::vecStore(
    const DxbcValue&        dst,
    const DxbcValue&        src,
          DxbcComponentMask mask) {
    DxbcValue result;
    result.type = dst.type;
    
    if (dst.type.componentCount == 1) {
      // Both values are scalar, so the first component
      // of the write mask decides which one to take.
      result.valueId = mask.test(0)
        ? src.valueId : dst.valueId;
    } else if (src.type.componentCount == 1) {
      // The source value is scalar. Since OpVectorShuffle
      // requires both arguments to be vectors, we have to
      // use OpCompositeInsert to modify the vector instead.
      const uint32_t componentId = mask.firstComponent();
      
      result.valueId = m_module.opCompositeInsert(
        this->defValueType(result.type),
        src.valueId, dst.valueId,
        1, &componentId);
    } else {
      // Both arguments are vectors. We can determine which
      // components to take from which vector and use the
      // OpVectorShuffle instruction.
      std::array<uint32_t, 4> components;
      uint32_t srcComponentId = dst.type.componentCount;
      
      for (uint32_t i = 0; i < dst.type.componentCount; i++)
        components[i] = mask.test(i) ? srcComponentId++ : i;
      
      result.valueId = m_module.opVectorShuffle(
        this->defValueType(result.type),
        dst.valueId, src.valueId,
        dst.type.componentCount,
        components.data());
    }
    
    return result;
  }
  
  
  DxbcValue DxbcCodeGen::regLoad(const DxbcPointer& ptr) {
    DxbcValue result;
    result.type    = ptr.type.valueType;
    result.valueId = m_module.opLoad(
      this->defValueType(result.type),
      ptr.valueId);
    return result;
  }
  
  
  void DxbcCodeGen::regStore(
    const DxbcPointer&      ptr,
    const DxbcValue&        val,
          DxbcComponentMask mask) {
    if (ptr.type.valueType.componentCount != val.type.componentCount) {
      // In case we write to only a part of the destination
      // register, we need to load the previous value first
      // and then update the given components.
      DxbcValue tmp = this->regLoad(ptr);
                tmp = this->vecStore(tmp, val, mask);
                
      m_module.opStore(ptr.valueId, tmp.valueId);
    } else {
      // All destination components get written, so we don't
      // need to load and modify the target register first.
      m_module.opStore(ptr.valueId, val.valueId);
    }
  }
  
  
  Rc<DxbcCodeGen> DxbcCodeGen::create(
    const DxbcProgramVersion& version) {
    switch (version.type()) {
      case DxbcProgramType::VertexShader:
        return new DxbcVsCodeGen();
      
      default:
        throw DxvkError(str::format(
          "DxbcCodeGen::create: Unsupported program type: ",
          version.type()));
    }
  }
  
  
  uint32_t DxbcCodeGen::defScalarType(DxbcScalarType type) {
    switch (type) {
      case DxbcScalarType::Uint32 : return m_module.defIntType(32, 0);
      case DxbcScalarType::Uint64 : return m_module.defIntType(64, 0);
      case DxbcScalarType::Sint32 : return m_module.defIntType(32, 1);
      case DxbcScalarType::Sint64 : return m_module.defIntType(64, 1);
      case DxbcScalarType::Float32: return m_module.defFloatType(32);
      case DxbcScalarType::Float64: return m_module.defFloatType(64);
      
      default:
        throw DxvkError("DxbcCodeGen::defScalarType: Invalid scalar type");
    }
  }
  
  
  uint32_t DxbcCodeGen::defValueType(const DxbcValueType& type) {
    uint32_t typeId = this->defScalarType(type.componentType);
    
    if (type.componentCount > 1)
      typeId = m_module.defVectorType(typeId, type.componentCount);
    
    if (type.elementCount > 0)
      typeId = m_module.defArrayType(typeId, m_module.constu32(type.elementCount));
    
    return typeId;
  }
  
  
  uint32_t DxbcCodeGen::defPointerType(const DxbcPointerType& type) {
    uint32_t valueTypeId = this->defValueType(type.valueType);
    return m_module.defPointerType(valueTypeId, type.storageClass);
  }
  
  
  uint32_t DxbcCodeGen::defPerVertexBlock() {
    uint32_t s1f32 = this->defScalarType(DxbcScalarType::Float32);
    uint32_t v4f32 = this->defValueType(DxbcValueType(DxbcScalarType::Float32, 4, 0));
    uint32_t a2f32 = this->defValueType(DxbcValueType(DxbcScalarType::Float32, 1, 2));
    
    std::array<uint32_t, 4> members;
    members[PerVertex_Position]  = v4f32;
    members[PerVertex_PointSize] = s1f32;
    members[PerVertex_CullDist]  = a2f32;
    members[PerVertex_ClipDist]  = a2f32;
    
    uint32_t typeId = m_module.defStructType(
      members.size(), members.data());
    
    m_module.memberDecorateBuiltIn(typeId, PerVertex_Position, spv::BuiltInPosition);
    m_module.memberDecorateBuiltIn(typeId, PerVertex_PointSize, spv::BuiltInPointSize);
    m_module.memberDecorateBuiltIn(typeId, PerVertex_CullDist, spv::BuiltInCullDistance);
    m_module.memberDecorateBuiltIn(typeId, PerVertex_ClipDist, spv::BuiltInClipDistance);
    m_module.decorateBlock(typeId);
    
    m_module.setDebugName(typeId, "per_vertex");
    m_module.setDebugMemberName(typeId, PerVertex_Position, "position");
    m_module.setDebugMemberName(typeId, PerVertex_PointSize, "point_size");
    m_module.setDebugMemberName(typeId, PerVertex_CullDist, "cull_dist");
    m_module.setDebugMemberName(typeId, PerVertex_ClipDist, "clip_dist");
    return typeId;
  }
  
}