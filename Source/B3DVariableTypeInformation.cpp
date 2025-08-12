#include "B3DCommon.h"
#include "B3DVariableTypeInformation.h"

#include "B3DTypeLookup.h"

VariableTypeInformation::VariableTypeInformation(const VariableTypeInformation& other)
{
	TypeCategory = other.TypeCategory;
	TypeName = other.TypeName;
	QualifierFlags = other.QualifierFlags;
	PostProcessFlags = other.PostProcessFlags;
	ParameterFlags = other.ParameterFlags;
	ArraySize = other.ArraySize;

	if (other.UnderlyingType != nullptr)
	{
		UnderlyingType = std::make_unique<VariableTypeInformation>(*other.UnderlyingType);
	}
}

VariableTypeInformation& VariableTypeInformation::operator=(const VariableTypeInformation& other)
{
	TypeCategory = other.TypeCategory;
	TypeName = other.TypeName;
	QualifierFlags = other.QualifierFlags;
	PostProcessFlags = other.PostProcessFlags;
	ParameterFlags = other.ParameterFlags;
	ArraySize = other.ArraySize;

	if (other.UnderlyingType != nullptr)
	{
		UnderlyingType = std::make_unique<VariableTypeInformation>(*other.UnderlyingType);
	}
	else
	{
		UnderlyingType = nullptr;
	}

	return *this;
}

void VariableTypeInformation::SetParameterFlag(enum ParameterFlags flags, bool recursive)
{
	ParameterFlags |= (uint32_t)flags;

	if(recursive && UnderlyingType)
		UnderlyingType->SetParameterFlag(flags, true);
}

void VariableTypeInformation::UnsetParameterFlag(enum ParameterFlags flags, bool recursive)
{
	ParameterFlags &= ~(uint32_t)flags;

	if(recursive && UnderlyingType)
		UnderlyingType->UnsetParameterFlag(flags, true);
}

void VariableTypeInformation::SetPostProcessFlag(VariablePostProcessFlags flags, bool recursive)
{
	PostProcessFlags |= (uint32_t)flags;

	if (recursive && UnderlyingType)
		UnderlyingType->SetPostProcessFlag(flags, true);
}

const std::string& VariableTypeInformation::GetFirstWrappedOrSelfTypeName() const
{
	switch(TypeCategory)
	{
	default:
	case VariableTypeCategory::General: 
	case VariableTypeCategory::Primitive: 
	case VariableTypeCategory::String: 
	case VariableTypeCategory::ConstCharString: 
	case VariableTypeCategory::WString:
	case VariableTypeCategory::MonoObject: 
	case VariableTypeCategory::MonoReflectionType:
	case VariableTypeCategory::Path:
	case VariableTypeCategory::IReflectable:
		return TypeName;
	case VariableTypeCategory::Vector:
	case VariableTypeCategory::TInlineArray:
	case VariableTypeCategory::TArray:
	case VariableTypeCategory::Array:
	case VariableTypeCategory::SharedPointer:
	case VariableTypeCategory::ResourceHandle:
	case VariableTypeCategory::GameObjectHandle:
	case VariableTypeCategory::Flags:
	case VariableTypeCategory::AsyncOp:
		return AssertGetUnderlyingType().TypeName;
	}
}

const std::string& VariableTypeInformation::GetLastWrappedOrSelfTypeName() const
{
	if (UnderlyingType)
		return UnderlyingType->GetLastWrappedOrSelfTypeName();

	return TypeName;
}

bool VariableTypeInformation::IsOutputParameter(const TypeMappingInformation& typeMappingInformation) const
{
	// Special case for types that are passed as native pointers
	if(TypeCategory == VariableTypeCategory::MonoObject || TypeCategory == VariableTypeCategory::MonoReflectionType)
	{
		assert(IsQualifierFlagSet(VariableQualifierFlags::IsPointer));

		// Output parameter only if it's a reference to pointer
		return IsQualifierFlagSet(VariableQualifierFlags::IsReference);
	}
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement)
	{
		// GUIElements are passed as raw pointers, so don't consider a non-const pointer an output
		return IsQualifierFlagSet(VariableQualifierFlags::IsReference) && !IsQualifierFlagSet(VariableQualifierFlags::IsConst);
	}

	return (IsQualifierFlagSet(VariableQualifierFlags::IsPointer) || IsQualifierFlagSet(VariableQualifierFlags::IsReference)) && !IsQualifierFlagSet(VariableQualifierFlags::IsConst);
}

