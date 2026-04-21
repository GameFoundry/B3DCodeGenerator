//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

struct TypeMappingInformation;

/** Determines the type of variable contained in VariableTypeInformation. */
enum class VariableTypeCategory
{
	General, /**< Type is not a recognized built-in type. */
	Primitive, /**< int, bool, float, etc. */
	Vector, /**< Vector<T>. Will also provide an underlying type information for T. */
	SharedPointer, /**< Shared<T>. Will also provide an underlying type information for T. */
	ResourceHandle, /**< ResourceHandle<T>. Will also provide an underlying type information for T. */
	GameObjectHandle, /**< GameObjectHandle<T>. Will also provide an underlying type information for T. */
	String, /**< String. */
	WString, /**< WString. */
	Flags, /**< Flags<T>. Will also provide an underlying type information for T. */
	Array,/**< Array<T>. Will also provide an underlying type information for T. */
	MonoObject, /**< MonoObject. Must always be passed as raw pointer. */
	Path, /**< Path */
	AsyncOp, /**< AsyncOp<T>. Will also provide an underlying type information for T. */
	TInlineArray, /**< TArray<T, InlineContainerAllocator<Size>>. Will also provide an underlying type information for T. */
	TArray, /**< TArray<T>. Will also provide an underlying type information for T. */
	ConstCharString, /**< const char* treated as a string. */
	MonoReflectionType, /**< MonoReflectionType. Must always be passed as raw pointer. */
	IReflectable, /**< Generic type inheriting from IReflectable interface. C# passed to native code will be serialized and converted to IReflectable. */
};

/** Qualifiers applied to a type in VariableTypeInformation. */
enum class VariableQualifierFlags
{
	None = 0,
	IsPointer = 1 << 0,
	IsReference = 1 << 1,
	IsConst = 1 << 2,
};

/** Various flags that can be added to VariableTypeInformation on post-processing. */
enum class VariablePostProcessFlags
{
	None = 0,
	IsStructWrapperUsed = 1 << 0, /**< Special flag to be set during post-processing. Signals to the user that a struct wrapper had to be generated and should be used instead of the native type. */
	IsReferencingBaseClass = 1 << 1, /**< Special flag to be set during post-processing. Signals to the user that a parameter, return value or a field is referencing a script exported base class. */
};

/** Various flags that can be added to VariableTypeInformation, specific to method parameters. */
enum class ParameterFlags
{
	None = 0,
	VarParams = 1 << 0, /**< lets the generator know to generate a variable number of parameters in place of this parameter. */
	AsResourceRef = 1 << 1, /**< lets the generator know to pass a resource as a resource reference, rather than directly. */
};

/** Contains type information about a parameter, return value, field or local variable usage. */
struct VariableTypeInformation
{
	VariableTypeInformation() = default;
	VariableTypeInformation(const VariableTypeInformation& other);
	VariableTypeInformation& operator=(const VariableTypeInformation& other);

	bool IsParameterFlagSet(enum ParameterFlags flags) const { return (ParameterFlags & (uint32_t)flags) != 0; }
	bool IsPostProcessFlagSet(VariablePostProcessFlags flags) const { return (PostProcessFlags & (uint32_t)flags) != 0; }
	bool IsQualifierFlagSet(VariableQualifierFlags flags) const { return (QualifierFlags & (uint32_t)flags) != 0; }

	void SetParameterFlag(enum ParameterFlags flags, bool recursive);
	void UnsetParameterFlag(enum ParameterFlags flags, bool recursive);
	void SetPostProcessFlag(VariablePostProcessFlags flags, bool recursive);

	/** Returns true if there is not type information assigned. */
	bool IsEmpty() const { return TypeName.empty(); }

	/** Returns true if the variable type is a non-const pointer or reference, which is recognized as a parameter output. */
	bool IsOutputParameter(const TypeMappingInformation& typeMappingInformation) const;

	/** Checks if the type category of the vector a native array, Vector, or TArray. If @p includeNative is false, native arrays won't be counted. */
	bool IsArrayOrVector(bool includeNative = true) const
	{
		return (includeNative && TypeCategory == VariableTypeCategory::Array) || TypeCategory == VariableTypeCategory::TInlineArray || TypeCategory == VariableTypeCategory::TArray || TypeCategory == VariableTypeCategory::Vector;
	}

	/** Checks if the type category is a shared pointer, resource handle or a game object handle. */
	bool IsPointerOrHandle() const
	{
		return TypeCategory == VariableTypeCategory::SharedPointer || TypeCategory == VariableTypeCategory::GameObjectHandle || TypeCategory == VariableTypeCategory::ResourceHandle;
	}

	/** Returns the underlying type. Asserts if the underlying type doesn't exist. */
	const VariableTypeInformation& AssertGetUnderlyingType() const
	{
		assert(UnderlyingType != nullptr);
		return *UnderlyingType;
	}

	/** If this type wraps another type, returns the wrapped type name. Otherwise, returns the name of this type. If there are multiple nested wrapped types this only returns the first one. */
	const std::string& GetFirstWrappedOrSelfTypeName() const;

	/** If this type wraps another type, returns the wrapped type name. Otherwise, returns the name of this type. If there are multiple nested wrapped types this returns the last one. */
	const std::string& GetLastWrappedOrSelfTypeName() const;

	VariableTypeCategory TypeCategory = VariableTypeCategory::General;
	std::string TypeName;
	std::unique_ptr<VariableTypeInformation> UnderlyingType;
	uint32_t QualifierFlags = (uint32_t)VariableQualifierFlags::None;
	uint32_t PostProcessFlags = (uint32_t)VariablePostProcessFlags::None;
	uint32_t ParameterFlags = (uint32_t)ParameterFlags::None;
	uint32_t ArraySize = 0; /**< Size of a native array, or SmallVector. */
};
