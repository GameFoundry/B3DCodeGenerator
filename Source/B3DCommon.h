#pragma once

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/HeaderSearch.h"
#include "llvm/Support/Path.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Comment.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;
using namespace clang;

extern const char *const kBuiltinComponentType;
extern const char *const kBuiltinSceneObjectType;
extern const char *const kBuiltinResourceType;
extern const char *const kBuiltinModuleType;
extern const char *const kBuiltinGUIElementType;
extern const char *const kBuiltinReflectableType;

extern std::string sFrameworkCppNs;
extern std::string sEditorCppNs;
extern std::string sFrameworkCsNs;
extern std::string sEditorCsNs;
extern std::string sFrameworkExportMacro;
extern std::string sEditorExportMacro;
extern std::string sFrameworkCopyrightNotice;
extern std::string sEditorCopyrightNotice;

/** Determines the high level type of the exported class/struct declaration. */
enum class ExportedClassTypeCategory
{
	Component, /**< Child of native builtin Component type. */
	SceneObject,/**< Child of native builtin SceneObject type. */
	Resource, /**< Child of native builtin Resource type. */
	GUIElement, /**< Child of native builtin GUIElementBase type. */
	Class, /**< Generic class (no known builtin type is a base). */
	ReflectableClass, /**< Child of native builtin IReflectable type. */
	Struct, /**< Generic struct (no known builtin type is a base). */
	Enum, /**< enum or enum class. */
	Primitive, /**< int, float, bool, etc. */
	String, /**< Builtin String type. */
	WString, /**< Builtin WString type. */
	Path, /**< Builtin Path type. */
	MonoObject /**< Builtin MonoObject type. */
};

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
	MonoObject, /**< MonoObject. */
	ComponentOrActor, /**< ComponentOrActor<T>. Will also provide an underlying type information for T. */
	Path, /**< Path */
	AsyncOp, /**< AsyncOp<T>. Will also provide an underlying type information for T. */
	SmallVector, /**< SmallVector<T>. Will also provide an underlying type information for T. */
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

// TODO - Refactor type parsing to output this struct instead, replace uses of TypeFlags
// - Move all the getters that check flags here
/** Contains type information about a parameter, return value, field or local variable usage. */
struct VariableTypeInformation
{
	VariableTypeInformation() = default;
	VariableTypeInformation(const VariableTypeInformation& other);
	VariableTypeInformation& operator=(const VariableTypeInformation& other);

	bool IsParameterFlagSet(enum ParameterFlags flags) const { return (ParameterFlags & (uint32_t)flags) != 0; }
	bool IsPostProcessFlagSet(VariablePostProcessFlags flags) const { return (PostProcessFlags & (uint32_t)flags) != 0; }
	bool IsQualifierFlagSet(VariableQualifierFlags flags) const { return (QualifierFlags & (uint32_t)flags) != 0; }

	void UnsetParameterFlag(enum ParameterFlags flags, bool recursive);
	void SetPostProcessFlag(VariablePostProcessFlags flags, bool recursive);

	/** Returns true if there is not type information assigned. */
	bool IsEmpty() const { return TypeName.empty(); }

	/** Returns true if the variable type is a non-const pointer or reference, which is recognized as a parameter output. */
	bool IsOutputParameter() const { return (IsQualifierFlagSet(VariableQualifierFlags::IsPointer) || IsQualifierFlagSet(VariableQualifierFlags::IsReference)) && !IsQualifierFlagSet(VariableQualifierFlags::IsConst); }

	/** Checks if the type category of the vector a native array, Vector, or SmallVector. */
	bool IsArrayOrVector() const
	{
		return TypeCategory == VariableTypeCategory::Array || TypeCategory == VariableTypeCategory::SmallVector || TypeCategory == VariableTypeCategory::Vector;
	}

	/** Checks if the type category is a shared pointer, resource handle or a game object handle. */
	bool IsPointerOrHandle() const
	{
		return TypeCategory == VariableTypeCategory::SharedPointer || TypeCategory == VariableTypeCategory::GameObjectHandle || TypeCategory == VariableTypeCategory::ResourceHandle || TypeCategory == VariableTypeCategory::ComponentOrActor;
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

inline VariableTypeInformation::VariableTypeInformation(const VariableTypeInformation& other)
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

inline VariableTypeInformation& VariableTypeInformation::operator=(const VariableTypeInformation& other)
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

inline void VariableTypeInformation::UnsetParameterFlag(enum ParameterFlags flags, bool recursive)
{
	ParameterFlags &= ~(uint32_t)flags;

	if(recursive && UnderlyingType)
		UnderlyingType->UnsetParameterFlag(flags, true);
}

inline void VariableTypeInformation::SetPostProcessFlag(VariablePostProcessFlags flags, bool recursive)
{
	PostProcessFlags |= (uint32_t)flags;

	if (recursive && UnderlyingType)
		UnderlyingType->SetPostProcessFlag(flags, true);
}

inline const std::string& VariableTypeInformation::GetFirstWrappedOrSelfTypeName() const
{
	switch(TypeCategory)
	{
	default:
	case VariableTypeCategory::General: 
	case VariableTypeCategory::Primitive: 
	case VariableTypeCategory::String: 
	case VariableTypeCategory::WString:
	case VariableTypeCategory::MonoObject: 
	case VariableTypeCategory::Path:
		return TypeName;
	case VariableTypeCategory::Vector:
	case VariableTypeCategory::SmallVector:
	case VariableTypeCategory::Array:
	case VariableTypeCategory::SharedPointer:
	case VariableTypeCategory::ResourceHandle:
	case VariableTypeCategory::GameObjectHandle:
	case VariableTypeCategory::Flags:
	case VariableTypeCategory::ComponentOrActor:
	case VariableTypeCategory::AsyncOp:
		return AssertGetUnderlyingType().TypeName;
	}
}

inline const std::string& VariableTypeInformation::GetLastWrappedOrSelfTypeName() const
{
	if (UnderlyingType)
		return UnderlyingType->GetLastWrappedOrSelfTypeName();

	return TypeName;
}

enum class TypeFlags // TODO - To be removed
{
	Primitive = 1 << 0,
	IsOutputQualifier = 1 << 1, /**< True if the type qualifiers don't contain 'const', and are a pointer or a reference type. */
	Vector = 1 << 2,
	IsNativePointerQualifier = 1 << 3,
	IsSharedPointerQualifier = 1 << 4,
	IsReferenceQualifier = 1 << 5,
	IsResourceHandleQualifier = 1 << 6,
	IsGameObjectHandleQualifier = 1 << 7,
	String = 1 << 8,
	WString = 1 << 9,
	IsStructWrapperUsed = 1 << 11, /**< Special flag to be set during post-processing. Signals to the user that a struct wrapper had to be generated and should be used instead of the native type. */
	FlagsEnum = 1 << 12,
	IsReferencingBaseClass = 1 << 13, /**< Special flag to be set during post-processing. Signals to the user that a parameter, return value or a field is referencing a script exported base class. */
	Array = 1 << 14,
	MonoObject = 1 << 15,
	VarParams = 1 << 16, /**< Flag for parameters only, that lets the generator know to generate a variable number of parameters in place of this parameter. */
	AsResourceRef = 1 << 17, /**< Flag for parameters only, that lets the generator know to pass a resource as a resource reference, rather than directly. */
	ComponentOrActor = 1 << 18,
	Path = 1 << 19,
	AsyncOp = 1 << 20,
	SmallVector = 1 << 21
};

enum class MethodFlags
{
	Static = 1 << 0,
	External = 1 << 1,
	Constructor = 1 << 2,
	PropertyGetter = 1 << 3,
	PropertySetter = 1 << 4,
	InteropOnly = 1 << 5,
	Callback = 1 << 6,
	FieldWrapper = 1 << 7,
	CSOnly = 1 << 8,
};

enum class CSVisibility
{
	Public,
	Internal,
	Private
};

enum class ExportFlags
{
	ExportAsStruct = 1 << 0,
	PropertyGetter = 1 << 1,
	PropertySetter = 1 << 2,
	ExternalMethod = 1 << 3,
	ExternalConstructor = 1 << 4,
	Exclude = 1 << 5,
	InteropOnly = 1 << 6,
	FrameworkAPI = 1 << 7,
	EngineAPI = 1 << 8,
	EditorAPI = 1 << 9
};

enum class ClassFlags
{
	IsBase = 1 << 0,
	IsModule = 1 << 1,
	IsTemplateInst = 1 << 2,
	IsStruct = 1 << 3,
	HideInInspector = 1 << 4
};

enum class StyleFlags
{
	ForceHide = 1 << 0,
	ForceShow = 1 << 1,
	AsSlider = 1 << 2,
	AsLayerMask = 1 << 3,
	Range = 1 << 4,
	Step = 1 << 5,
	Category = 1 << 6,
	Order = 1 << 7,
	NotNull = 1 << 8,
	PassByCopy = 1 << 9,
	ApplyOnDirty = 1 << 10,
	AsQuaternion = 1 << 11,
	Inline = 1 << 12,
	LoadOnAssign = 1 << 13,
	HDR = 1 << 14,
};

enum class ApiFlags
{
	Framework = 1 << 0,
	Engine = 1 << 1,
	Editor = 1 << 2,
	Any = Framework | Engine
};

struct ExportStyle
{
	float rangeMin;
	float rangeMax;
	float step;
	int order;
	std::string category;
	int flags = 0;
};

/**
 * Contains information about how a native type maps to a script type.
 *
 * Note we need this separate from ClassInfo and StructInfo as occasionally we need to provide type mapping for types that won't be generated (e.g. are builtin)
 */
struct TypeMappingInformation // TODO - Add a new TypeMapping file/class. Registering a new struct/class/enum should auto-register this type as well. And a special method for registering existing/builtin types
// TODO - GetTypeInfo should be moved there, and built-in types should not be constructed on the fly (But probably not important at the moment)
{
	TypeMappingInformation() {}

	TypeMappingInformation(SmallVector<std::string, 4> nativeNamespace, const std::string& scriptName, ::ExportedClassTypeCategory typeCategory, const std::string& nativeFile, const std::string& destFile)
		:NativeNamespace(std::move(nativeNamespace)), ScriptTypeName(scriptName), TypeCategory(typeCategory), NativeFile(nativeFile), InteropFile(destFile), EditorInteropFile(destFile)
	{ }

	TypeMappingInformation(SmallVector<std::string, 4> nativeNamespace, const std::string& scriptName, ::ExportedClassTypeCategory typeCategory, const std::string& nativeFile, const std::string& destFile,
		const std::string& destFileEditor)
		:NativeNamespace(std::move(nativeNamespace)), ScriptTypeName(scriptName), TypeCategory(typeCategory), NativeFile(nativeFile), InteropFile(destFile), EditorInteropFile(destFileEditor)
	{ }

	std::string ScriptTypeName; /**< Name of the type in the script code. */
	SmallVector<std::string, 4> NativeNamespace; /**< Namespace in which the native type is located in. Used for e.g. forward declares in generated native interop code. */
	std::string NativeFile; /**< File in which the native type is defined in. Used for resolving includes. */
	std::string InteropFile; /**< File in which the interop for this type is defined in. Used for resolving includes. */
	std::string EditorInteropFile; /**< Same as @p InteropFile, but if a type is exported in both framework and editor, then we need to generate two interop files. */
	::ExportedClassTypeCategory TypeCategory; /**< Determines a high level category that this type belongs to. */
	BuiltinType::Kind EnumUnderlyingType; /**< Underlying primitive type for enum or enum class. */
};

struct VariableBase
{
	VariableTypeInformation TypeInformation;

	std::string typeName; // TODO - Remove
	unsigned arraySize; // TODO - Remove
	int flags; // TODO - Remove
};

struct VariableInformation : VariableBase
{
	std::string Name;

	std::string DefaultValue;
	std::string DefaultValueType;
};

struct ReturnInfo : VariableBase
{ };

/** Represents a reference to another type, method or parameter within a comment. */
struct CommentReference
{
	uint32_t PositionInText; /**< Position in the corresponding text, at which to insert the reference. */
	std::string Name; /**< Name of the type, method, field or parameter that's being referenced. */
};

/** Contains a single paragraph of comment text for a particular type, method, field or parameter. */
struct CommentText
{
	std::string Text;
	SmallVector<CommentReference, 2> ParameterReferences; /**< Locations within @p text at which method parameters are referenced. Only relevant if the current comment is part of a method comment. */
	SmallVector<CommentReference, 2> DeclarationReferences; /**< Locations within @p text at which other declarations are referenced (e.g. other types, methods, fields). */
};

/** Contains zero or multiple paragraphs of comment text for a method parameter. */
struct CommentParameterEntry
{
	std::string Name; /**< Name of the parameter that's being commented. */
	SmallVector<CommentText, 2> comments; /**< Zero or multiple comment text paragraphs. */
};

/** Contains comment text for a particular type, method or field. */
struct CommentEntry
{
	SmallVector<CommentText, 2> brief; /**< Summary comment describing the type, method or field. */

	SmallVector<CommentParameterEntry, 4> params; /**< Comments for method parameters, if this is a method comment. */
	SmallVector<CommentText, 2> returns; /**< Zero or multiple comment paragraphs describing method return value, if this is a method comment. */
};

struct FieldInfo : VariableInformation
{
	CommentEntry documentation;
	ExportStyle style;
};

struct TemplateParamInfo
{
	std::string type;
};

struct MethodInfo
{
	std::string sourceName;
	std::string interopName;
	std::string scriptName;
	CSVisibility visibility;
	ApiFlags api;

	ReturnInfo returnInfo;
	std::vector<VariableInformation> paramInfos;
	CommentEntry documentation;

	std::string externalClass;
	int flags;
	ExportStyle style;
};

struct PropertyInfo
{
	VariableTypeInformation TypeInformation;
	std::string name;

	std::string getter;
	std::string setter;

	CSVisibility visibility;
	ApiFlags api;
	bool isStatic;
	ExportStyle style;
	CommentEntry documentation;
};

struct ClassInfo
{
	std::string name;
	std::string cleanName;
	CSVisibility visibility;
	ApiFlags api;
	int flags;
	SmallVector<std::string, 4> ns;
	SmallVector<TemplateParamInfo, 0> templParams;

	std::vector<MethodInfo> ctorInfos;
	std::vector<PropertyInfo> propertyInfos;
	std::vector<MethodInfo> methodInfos;
	std::vector<MethodInfo> eventInfos;
	std::vector<FieldInfo> fieldInfos;
	std::string baseClass;

	CommentEntry documentation;
	std::string module;
};

struct ExternalClassInfos
{
	std::vector<MethodInfo> methods;
};

struct SimpleConstructorInfo
{
	std::vector<VariableInformation> params;
	std::unordered_map<std::string, std::string> fieldAssignments;
	CommentEntry documentation;
};

struct StructInfo
{
	std::string name;
	std::string cleanName;
	std::string interopName;
	std::string baseClass;
	CSVisibility visibility;
	ApiFlags api;
	SmallVector<std::string, 4> ns;
	SmallVector<TemplateParamInfo, 0> templParams;

	std::vector<SimpleConstructorInfo> ctors;
	std::vector<FieldInfo> fields;
	bool requiresInterop : 1;
	bool isTemplateInst : 1;

	CommentEntry documentation;
	std::string module;
};

/** Information about a single entry within an enum. */
struct EnumEntryInfo
{
	std::string NativeName;
	std::string ScriptName;
	std::string Value;
	CommentEntry Documentation;
};

struct EnumInfo
{
	std::string name;
	std::string scriptName;
	CSVisibility visibility;
	ApiFlags api;
	SmallVector<std::string, 4> ns;

	std::string explicitType;
	std::unordered_map<int, EnumEntryInfo> entries;

	CommentEntry documentation;
	std::string module;
};

struct ForwardDeclInfo
{
	SmallVector<std::string, 4> ns;
	std::string name;
	bool isStruct;
	SmallVector<TemplateParamInfo, 0> templParams;

	bool operator==(const ForwardDeclInfo& rhs) const
	{
		return name == rhs.name && ns == rhs.ns;
	}
};

template<>
struct std::hash<ForwardDeclInfo>
{
	std::size_t operator()(const ForwardDeclInfo& value) const
	{
		std::hash<std::string> hasher;
		size_t hash = hasher(value.name);

		for (auto& entry : value.ns)
			hash = hash_combine(hash, hasher(entry));
		
		return hash;
	}
};

struct FileInfo
{
	std::vector<ClassInfo> classInfos;
	std::vector<StructInfo> structInfos;
	std::vector<EnumInfo> enumInfos;

	std::unordered_set<ForwardDeclInfo> forwardDeclarations;
	std::vector<std::string> referencedHeaderIncludes;
	std::vector<std::string> referencedSourceIncludes;
	bool inEditor;
};

enum class IncludeType
{
	None,
	IncludeInHeader = 1 << 0,
	IncludeInImplementation = 1 << 1,
	ForwardDeclare = 1 << 2,
	ForwardDeclareAndIncludeInImplementation = ForwardDeclare | IncludeInImplementation
};

struct IncludeInfo
{
	IncludeInfo() { }
	IncludeInfo(const std::string& typeName, const TypeMappingInformation& typeInfo, IncludeType originIncludeFlags, 
		IncludeType interopIncludeFlags, bool isStruct = false, bool isEditor = false)
		: typeName(typeName), typeInfo(typeInfo), originIncludeFlags(originIncludeFlags)
		, interopIncludeFlags(interopIncludeFlags), isStruct(isStruct), isEditor(isEditor)
	{ }

	std::string typeName;
	TypeMappingInformation typeInfo;
	IncludeType originIncludeFlags;
	IncludeType interopIncludeFlags;
	bool isStruct;
	bool isEditor;
};

struct IncludesInfo
{
	bool requiresResourceManager = false;
	bool requiresGameObjectManager = false;
	bool requiresRRef = false;
	bool requiresRTTI = false;
	bool requiresAsyncOp = false;
	std::unordered_map<std::string, IncludeInfo> includes;
	std::unordered_map<std::string, ForwardDeclInfo> fwdDecls;
};

struct CommentMethodInformation
{
	SmallVector<std::string, 3> params;
	CommentEntry comment;
};

struct CommentInformation
{
	std::string name;
	std::string fullName;

	SmallVector<std::string, 2> namespaces;
	SmallVector<CommentMethodInformation, 2> overloads;

	CommentEntry comment;
	bool isFunction = false;
};

struct BaseClassInfo
{
	std::vector<std::string> childClasses;
};

enum FileType
{
	FT_ENGINE_H,
	FT_ENGINE_CPP,
	FT_EDITOR_H,
	FT_EDITOR_CPP,
	FT_ENGINE_CS,
	FT_EDITOR_CS,
	FT_COUNT // Keep at end
};

inline bool hasAPIBED(ApiFlags api)
{
	return ((int)api & (int)ApiFlags::Editor) != 0;
}

inline bool hasAPIB3D(ApiFlags api)
{
	return ((int)api & (int)ApiFlags::Engine) != 0;
}

inline bool hasAPIBSF(ApiFlags api)
{
	return ((int)api & (int)ApiFlags::Framework) != 0;
}

inline bool isValidAPI(ApiFlags api, bool editor)
{
   return (editor && hasAPIBED(api)) || (!editor && (hasAPIB3D(api) || hasAPIBSF(api)));
}

/** Contains a map of native types to script types. The key is the native name as provided in ClassInfo.Name, StructInfo.Name or EnumInfo.Name. */
extern std::unordered_map<std::string, TypeMappingInformation> NativeToScriptTypeMap;
extern std::unordered_map<std::string, FileInfo> outputFileInfos;
extern std::unordered_map<std::string, ExternalClassInfos> externalClassInfos;
extern std::unordered_map<std::string, BaseClassInfo> baseClassLookup;

inline StructInfo* FindStructInformation(const std::string& name)
{
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& structInfo : fileInfo.second.structInfos)
		{
			if (structInfo.name == name)
				return &structInfo;
		}
	}

	return nullptr;
};

inline ClassInfo* FindClassInformation(const std::string& name, bool isEditor)
{
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			if (classInfo.name != name)
				continue;

			// Two versions of editor and Framework class migth exist, make sure to pick the right one
			if((isEditor && classInfo.api == ApiFlags::Framework) || (!isEditor &&  hasAPIBED(classInfo.api)))
				continue;

			return &classInfo;
		}
	}

	return nullptr;
}

inline EnumInfo* FindEnumInformation(const std::string& name)
{
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& enumInfo : fileInfo.second.enumInfos)
		{
			if (enumInfo.name == name)
				return &enumInfo;
		}
	}

	return nullptr;
}

inline bool mapBuiltinTypeToCSType(BuiltinType::Kind kind, std::string& output)
{
	switch (kind)
	{
	case BuiltinType::Void:
		output = "void";
		return true;
	case BuiltinType::Bool:
		output = "bool";
		return true;
	case BuiltinType::Char_S:
		output = "byte";
		return true;
	case BuiltinType::Char_U:
		output = "byte";
		return true;
	case BuiltinType::SChar:
		output = "byte";
		return true;
	case BuiltinType::Short:
		output = "short";
		return true;
	case BuiltinType::Int:
		output = "int";
		return true;
	case BuiltinType::Long:
		output = "long";
		return true;
	case BuiltinType::LongLong:
		output = "long";
		return true;
	case BuiltinType::UChar:
		output = "byte";
		return true;
	case BuiltinType::UShort:
		output = "short";
		return true;
	case BuiltinType::UInt:
		output = "int";
		return true;
	case BuiltinType::ULong:
		output = "long";
		return true;
	case BuiltinType::ULongLong:
		output = "long";
		return true;
	case BuiltinType::Float:
		output = "float";
		return true;
	case BuiltinType::Double:
		output = "double";
		return true;
	case BuiltinType::WChar_S:
	case BuiltinType::WChar_U:
		output = "short";
		return true;
	case BuiltinType::Char16:
		output = "short";
		return true;
	case BuiltinType::Char32:
		output = "int";
		return true;
	default:
		break;
	}

	errs() << "Unrecognized builtin type found.\n";
	return false;
}

inline std::string mapCppTypeToCSType(const std::string& cppType)
{
	if (cppType == "int8_t")
		return "sbyte";

	if (cppType == "uint8_t")
		return "byte";

	if (cppType == "int16_t")
		return "short";

	if (cppType == "uint16_t")
		return "ushort";

	if (cppType == "int32_t")
		return "int";

	if (cppType == "uint32_t")
		return "int";

	if (cppType == "int64_t")
		return "long";

	if (cppType == "uint64_t")
		return "ulong";

	if (cppType == "wchar_t")
		return "char";

	if (cppType == "char16_t")
		return "ushort";

	if (cppType == "char32_t")
		return "uint";

	return cppType;
}

inline std::string getCSLiteralSuffix(const std::string& cppType)
{
	if (cppType == "float")
		return "f";

	return "";
}

/** Returns the information about a native type maps to a script type. */
inline TypeMappingInformation GetNativeToScriptTypeMapping(const std::string& typeName)
{
	auto iterFind = NativeToScriptTypeMap.find(typeName);
	if (iterFind == NativeToScriptTypeMap.end())
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = mapCppTypeToCSType(typeName);
		outType.TypeCategory = ::ExportedClassTypeCategory::Primitive;

		errs() << "Unable to map type \"" << typeName << "\". Assuming same name as source.\n";
		return outType;
	}
	
	return iterFind->second;
}

/**
 * Returns the information about how a native type maps to a script type. The provided type information supports extra information about how the type
 * is being used (e.g. passed as a pointer, reference, resource handle, array etc.), and will utilize this information to return the underlying type.
 */
inline TypeMappingInformation GetNativeToScriptTypeMapping(const VariableTypeInformation& typeInformation)
{
	switch (typeInformation.TypeCategory)
	{
	case VariableTypeCategory::Primitive:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = mapCppTypeToCSType(typeInformation.TypeName);
		outType.TypeCategory = ::ExportedClassTypeCategory::Primitive;

		return outType;
	}
	case VariableTypeCategory::String:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ::ExportedClassTypeCategory::String;

		return outType;
	}
	case VariableTypeCategory::WString:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ::ExportedClassTypeCategory::WString;

		return outType;
	}
	case VariableTypeCategory::Path:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ::ExportedClassTypeCategory::Path;

		return outType;
	}
	case VariableTypeCategory::MonoObject:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "object";
		outType.TypeCategory = ::ExportedClassTypeCategory::MonoObject;

		return outType;
	}
	case VariableTypeCategory::AsyncOp:
	{
		TypeMappingInformation underlyingTypeMapping;
		if (!typeInformation.UnderlyingType)
		{
			errs() << "Unable to map underlying type for \"" << typeInformation.TypeName << "\". No underlying type found. \n";

			underlyingTypeMapping.ScriptTypeName = "Unknown";
			underlyingTypeMapping.TypeCategory = ::ExportedClassTypeCategory::Class;
		}
		else
		{
			underlyingTypeMapping = GetNativeToScriptTypeMapping(*typeInformation.UnderlyingType);
		}

		TypeMappingInformation outType = underlyingTypeMapping;
		outType.ScriptTypeName = "AsyncOp<" + underlyingTypeMapping.ScriptTypeName + ">";

		return outType;
	}
	case VariableTypeCategory::ResourceHandle:
	{
		TypeMappingInformation underlyingTypeMapping;
		VariableTypeInformation underlyingType;
		if (!typeInformation.UnderlyingType)
		{
			errs() << "Unable to map underlying type for \"" << typeInformation.TypeName << "\". No underlying type found. \n";

			underlyingTypeMapping.ScriptTypeName = "Unknown";
			underlyingTypeMapping.TypeCategory = ::ExportedClassTypeCategory::Class;

			underlyingType.TypeName = "Unknown";
			underlyingType.TypeCategory = VariableTypeCategory::General;
		}
		else
		{
			underlyingType = *typeInformation.UnderlyingType;
			underlyingTypeMapping = GetNativeToScriptTypeMapping(*typeInformation.UnderlyingType);
		}

		if(typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
		{
			TypeMappingInformation outType = underlyingTypeMapping;

			if (underlyingType.TypeName == "Resource")
				outType.ScriptTypeName = "RRefBase";
			else
				outType.ScriptTypeName = "RRef<" + underlyingTypeMapping.ScriptTypeName + ">";

			return outType;
		}
		else
			return underlyingTypeMapping;
	}
	// Just forward the type resolve to the underlying type. Note we don't support nested vectors, arrays or shared pointers
	case VariableTypeCategory::Vector:
	case VariableTypeCategory::SmallVector:
	case VariableTypeCategory::Array:
	case VariableTypeCategory::GameObjectHandle:
	case VariableTypeCategory::ComponentOrActor:
	case VariableTypeCategory::Flags:
	case VariableTypeCategory::SharedPointer:
	{
		TypeMappingInformation underlyingTypeMapping;
		if (!typeInformation.UnderlyingType)
		{
			errs() << "Unable to map underlying type for \"" << typeInformation.TypeName << "\". No underlying type found. \n";

			underlyingTypeMapping.ScriptTypeName = "Unknown";
			underlyingTypeMapping.TypeCategory = ::ExportedClassTypeCategory::Class;
		}
		else
		{
			underlyingTypeMapping = GetNativeToScriptTypeMapping(*typeInformation.UnderlyingType);
		}

		return underlyingTypeMapping;
	}
	default:
	case VariableTypeCategory::General:
		return GetNativeToScriptTypeMapping(typeInformation.TypeName);
	}
}

inline const std::string& escapeXML(const std::string& data) 
{
	std::string::size_type first = data.find_first_of("\"&<>", 0);
	if (first == std::string::npos)
		return data;

	static std::string buffer;
	buffer.reserve((size_t)(data.size() * 1.1f));
	buffer.clear();

	for (size_t pos = 0; pos != data.size(); ++pos)
	{
		switch (data[pos])
		{
		case '&':  buffer.append("&amp;");       break;
		case '\"': buffer.append("&quot;");      break;
		case '\'': buffer.append("&apos;");      break;
		case '<':  buffer.append("&lt;");        break;
		case '>':  buffer.append("&gt;");        break;
		default:   buffer.append(&data[pos], 1); break;
		}
	}

	return buffer;
}

inline bool isInt64(const TypeMappingInformation& typeInfo) // TODO - Make TypeMappingInformation member
{
	return typeInfo.TypeCategory == ::ExportedClassTypeCategory::Primitive && (typeInfo.ScriptTypeName == "long" || typeInfo.ScriptTypeName == "ulong");
}

inline bool isInteger(const TypeMappingInformation& typeInfo) // TODO - Make TypeMappingInformation member
{
	return typeInfo.TypeCategory == ::ExportedClassTypeCategory::Primitive &&
		(typeInfo.ScriptTypeName == "int" || typeInfo.ScriptTypeName == "uint" ||
			typeInfo.ScriptTypeName == "long" || typeInfo.ScriptTypeName == "ulong" ||
			typeInfo.ScriptTypeName == "short" || typeInfo.ScriptTypeName == "ushort" ||
			typeInfo.ScriptTypeName == "byte");
}

inline bool isReal(const TypeMappingInformation& typeInfo) // TODO - Make TypeMappingInformation member
{
	return typeInfo.TypeCategory == ::ExportedClassTypeCategory::Primitive &&
		(typeInfo.ScriptTypeName == "float" || typeInfo.ScriptTypeName == "double");
}

inline bool isOutput(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsOutputQualifier) != 0;
}

inline bool isArray(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::Array) != 0;
}

inline bool isVector(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::Vector) != 0;
}

inline bool isSmallVector(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::SmallVector) != 0;
}

inline bool isArrayOrVector(int flags) //  TODO - To be removed
{
	return (flags & ((int)TypeFlags::Vector | (int)TypeFlags::Array | (int)TypeFlags::SmallVector)) != 0;
}

inline bool isFlagsEnum(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::FlagsEnum) != 0;
}

inline bool isSrcPointer(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsNativePointerQualifier) != 0;
}

inline bool isSrcReference(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsReferenceQualifier) != 0;
}

inline bool isSrcValue(int flags) //  TODO - To be removed
{
	int nonValueFlags = (int)TypeFlags::IsNativePointerQualifier | (int)TypeFlags::IsReferenceQualifier | (int)TypeFlags::IsSharedPointerQualifier |
		(int)TypeFlags::IsResourceHandleQualifier | (int)TypeFlags::IsGameObjectHandleQualifier;

	return (flags & nonValueFlags) == 0;
}

inline bool isSrcSPtr(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsSharedPointerQualifier) != 0;
}

inline bool isSrcRHandle(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsResourceHandleQualifier) != 0;
}

inline bool isSrcGHandle(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsGameObjectHandleQualifier) != 0;
}

inline bool isComplexStruct(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsStructWrapperUsed) != 0;
}

inline bool isBaseParam(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::IsReferencingBaseClass) != 0;
}

inline bool isVarParam(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::VarParams) != 0;
}

inline bool getPassAsResourceRef(int flags) //  TODO - To be removed
{
	return (flags & (int)TypeFlags::AsResourceRef) != 0;
}

inline bool isStruct(int flags)
{
	return (flags & (int)ClassFlags::IsStruct) != 0;
}

inline bool isHandleType(::ExportedClassTypeCategory type)
{
	return type == ::ExportedClassTypeCategory::Resource || type == ::ExportedClassTypeCategory::SceneObject || type == ::ExportedClassTypeCategory::Component;
}

inline bool isClassType(::ExportedClassTypeCategory type)
{
	return type == ::ExportedClassTypeCategory::Class || type == ::ExportedClassTypeCategory::ReflectableClass;
}

inline ApiFlags apiFromExportFlags(int flags)
{
	int output = 0;

	if((flags & (int)ExportFlags::EngineAPI) != 0)
		output |= (int)ApiFlags::Engine;

	if((flags & (int)ExportFlags::FrameworkAPI) != 0)
		output |= (int)ApiFlags::Framework;

	if((flags & (int)ExportFlags::EditorAPI) != 0)
		output |= (int)ApiFlags::Editor;

	if((int)output == 0)
		output = (int)ApiFlags::Any;

	return (ApiFlags)output;
}

inline bool isCSOnly(int flags)
{
	return (flags & (int)MethodFlags::CSOnly) != 0;
}

/**
 * Returns true if the provided type can be used as a return value from a C# method call.
 *
 * @param	typeInformation				Information about the native type to generate the interop type for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 */
inline bool CanBeReturned(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation) //  TODO - Move to generator class
{
	if (typeInformation.IsOutputParameter())
		return false;

	if (typeInformation.IsArrayOrVector())
		return true;

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
		return false;

	return true;
}

inline bool endsWith(const std::string& str, const std::string& end) 
{
	if (str.length() >= end.length()) 
		return (0 == str.compare(str.length() - end.length(), end.length(), end));

	return false;
}

/** Removes C++ templates parameters from the provided type name. */
inline std::string CleanTemplateParameters(const std::string& name)
{
	std::string cleanName;
	int lBracket = name.find_first_of('<');
	if (lBracket != -1)
	{
		cleanName = name.substr(0, lBracket);

		int rBracket = name.find_last_of('>');
		if (rBracket != -1 && rBracket > lBracket)
			cleanName += name.substr(lBracket + 1, rBracket - lBracket - 1);
		else
			cleanName += name.substr(lBracket + 1, name.size() - rBracket - 1);
	}
	else
		cleanName = name;

	return cleanName;
}

/** Returns the name for a type used for struct interop object, based on the original struct name. */
inline std::string GetStructInteropTypeName(const std::string& name)
{
	return "__" + CleanTemplateParameters(name) + "Interop";
}

inline void getDerivedClasses(const std::string& typeName, std::vector<std::string>& output, bool onlyDirect = false)
{
	auto iterFind = baseClassLookup.find(typeName);
	if(iterFind == baseClassLookup.end())
		return;

	for(auto& entry : iterFind->second.childClasses)
	{
		output.push_back(entry);

		if(!onlyDirect)
			getDerivedClasses(entry, output);
	}
}

inline MethodInfo findUnusedCtorSignature(const ClassInfo& classInfo) // TODO - Move to GeneratorUtility
{
	auto checkSignature = [](int numParams, const MethodInfo& info)
	{
		if ((int)info.paramInfos.size() != numParams)
			return true;

		for (auto& paramInfo : info.paramInfos)
		{
			if (paramInfo.TypeInformation.TypeName != "bool")
				return true;
		}

		return false;
	};

	int numBools = 1;
	while (true)
	{
		bool isSignatureValid = true;

		// Check normal constructors
		for (auto& entry : classInfo.ctorInfos)
		{
			if(!checkSignature(numBools, entry))
			{
				isSignatureValid = false;
				break;
			}
		}

		// Check external constructors
		if(isSignatureValid)
		{
			for (auto& entry : classInfo.methodInfos)
			{
				bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
				if (!isConstructor)
					continue;

				if(!checkSignature(numBools, entry))
				{
					isSignatureValid = false;
					break;
				}
			}
		}

		if (isSignatureValid)
			break;

		numBools++;
	}

	MethodInfo output;
	output.sourceName = classInfo.cleanName;
	output.scriptName = classInfo.cleanName;
	output.flags = (int)MethodFlags::Constructor;
	output.visibility = CSVisibility::Private;

	for (int i = 0; i < numBools; i++)
	{
		VariableInformation paramInfo;
		paramInfo.Name = "__dummy" + std::to_string(i);
		paramInfo.typeName = "bool";
		paramInfo.flags = (int)TypeFlags::Primitive;

		paramInfo.TypeInformation.TypeName = "bool";
		paramInfo.TypeInformation.TypeCategory = VariableTypeCategory::Primitive;

		output.paramInfos.push_back(paramInfo);
	}

	return output;
}

inline void cleanAndPrepareFolder(const StringRef& folder) // TODO - Move to Generator common
{
	if (sys::fs::exists(folder))
	{
		std::error_code ec;
		for (sys::fs::directory_iterator file(folder, ec), fileEnd; file != fileEnd && !ec; file.increment(ec))
			sys::fs::remove(file->path());
	}

	sys::fs::create_directories(folder);
}

inline std::string getRelativeTo(const StringRef& path, const StringRef& relativeTo) // TODO - Move to Generator common
{
	SmallVector<char, 100> relativeToVector(relativeTo.begin(), relativeTo.end());

	vfs::getRealFileSystem()->makeAbsolute(relativeToVector);
	StringRef absRelativeTo(relativeToVector.data(), relativeToVector.size());

	SmallVector<char, 100> output;

	auto iterPath = sys::path::begin(path);
	auto iterRelativePath = sys::path::begin(absRelativeTo);

	bool foundRelative = false;
	for(; iterPath != sys::path::end(path) && iterRelativePath != sys::path::end(absRelativeTo); ++iterPath, ++iterRelativePath)
	{
		if (*iterPath != *iterRelativePath)
			break;

		foundRelative = true;
	}

	if (!foundRelative)
		return path.str();

	for(; iterRelativePath != sys::path::end(absRelativeTo); ++iterRelativePath)
		sys::path::append(output, "..");

	for (; iterPath != sys::path::end(path); ++iterPath)
		sys::path::append(output, *iterPath);

	sys::path::native(output, sys::path::Style::posix);
	return std::string(output.data(), output.size());
}

inline std::ofstream createFile(const std::string& filename, StringRef outputFolder) // TODO - Move to generator common
{
	std::string relativePath = "/" + filename;
	StringRef filenameRef(relativePath.data(), relativePath.size());

	SmallString<128> filepath = outputFolder;
	sys::path::append(filepath, filenameRef);

	std::ofstream output;
	output.open(filepath.str().str(), std::ios::out);

	return output;
}

inline std::string generateFileHeader(bool isBanshee) // TODO - Move to generator common
{
	std::stringstream output;
	if (isBanshee)
		output << sEditorCopyrightNotice;
	else
		output << sFrameworkCopyrightNotice;

	return output.str();
}

void GenerateCpp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode);
void GenerateCSharp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode);