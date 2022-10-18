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

	bool IsInt64() const;
	bool IsInteger() const;
	bool IsReal() const;
	bool IsHandleType() const;
	bool IsClassType() const;

	std::string ScriptTypeName; /**< Name of the type in the script code. */
	SmallVector<std::string, 4> NativeNamespace; /**< Namespace in which the native type is located in. Used for e.g. forward declares in generated native interop code. */
	std::string NativeFile; /**< File in which the native type is defined in. Used for resolving includes. */
	std::string InteropFile; /**< File in which the interop for this type is defined in. Used for resolving includes. */
	std::string EditorInteropFile; /**< Same as @p InteropFile, but if a type is exported in both framework and editor, then we need to generate two interop files. */
	ExportedClassTypeCategory TypeCategory; /**< Determines a high level category that this type belongs to. */
	BuiltinType::Kind EnumUnderlyingType; /**< Underlying primitive type for enum or enum class. */
};

inline bool TypeMappingInformation::IsInt64() const
{
	return TypeCategory == ExportedClassTypeCategory::Primitive && (ScriptTypeName == "long" || ScriptTypeName == "ulong");
}

inline bool TypeMappingInformation::IsInteger() const
{
	return TypeCategory == ExportedClassTypeCategory::Primitive && (ScriptTypeName == "int" || ScriptTypeName == "uint" || ScriptTypeName == "long" || ScriptTypeName == "ulong" || ScriptTypeName == "short" || ScriptTypeName == "ushort" || ScriptTypeName == "byte");
}

inline bool TypeMappingInformation::IsReal() const
{
	return TypeCategory == ExportedClassTypeCategory::Primitive && (ScriptTypeName == "float" || ScriptTypeName == "double");
}

inline bool TypeMappingInformation::IsHandleType() const
{
	return TypeCategory == ExportedClassTypeCategory::Resource || TypeCategory == ExportedClassTypeCategory::SceneObject || TypeCategory == ExportedClassTypeCategory::Component;
}

inline bool TypeMappingInformation::IsClassType() const
{
	return TypeCategory == ExportedClassTypeCategory::Class || TypeCategory == ExportedClassTypeCategory::ReflectableClass;
}

struct VariableBase
{
	VariableTypeInformation TypeInformation;
};

struct VariableInformation : VariableBase
{
	std::string Name;

	std::string DefaultValue; /**< Default value to assign to the variable. This will be a literal such as 5.0f, 5, true or "" if @p DefaultValueType is empty, or comma separated parameters to pass to the type constructor if not. */
	std::string DefaultValueType; /**< Type of value to construct in case the default value is a constructible type. Empty if default value is a literal.*/
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
	SmallVector<CommentText, 2> Comments; /**< Zero or multiple comment text paragraphs. */
};

/** Contains comment text for a particular type, method or field. */
struct CommentEntry
{
	SmallVector<CommentText, 2> Brief; /**< Summary comment describing the type, method or field. */

	SmallVector<CommentParameterEntry, 4> ParameterComments; /**< Comments for method parameters, if this is a method comment. */
	SmallVector<CommentText, 2> ReturnValueComments; /**< Zero or multiple comment paragraphs describing method return value, if this is a method comment. */
};

struct FieldInfo : VariableInformation
{
	CommentEntry Documentation;
	ExportStyle Style;
};

struct TemplateParamInfo
{
	std::string TypeName;
};

struct MethodInfo
{
	std::string NativeName;
	std::string InteropName;
	std::string ScriptName;
	CSVisibility Visibility = CSVisibility::Public;
	ApiFlags API = ApiFlags::Framework;

	ReturnInfo ReturnValue;
	std::vector<VariableInformation> Parameters;
	CommentEntry Documentation;

	std::string ExternalClass;
	int MethodFlags = 0;
	ExportStyle Style;

	/** Checks is the provided flag set on the method. */
	bool IsFlagSet(enum MethodFlags flag) const { return (MethodFlags & (int)flag) != 0; }
};

struct PropertyInfo
{
	VariableTypeInformation TypeInformation;
	std::string ScriptName;

	std::string GetterName;
	std::string SetterName;

	CSVisibility Visibility = CSVisibility::Public;
	ApiFlags API = ApiFlags::Framework;
	bool IsStatic = false;
	ExportStyle Style;
	CommentEntry Documentation;
};

struct GeneratedTypeInformation
{
	std::string NativeName;
	std::string BaseClassName;
	SmallVector<std::string, 4> Namespace;

	CSVisibility Visibility = CSVisibility::Public;
	ApiFlags API = ApiFlags::Framework;

	std::string NativeNameWithoutTemplateArguments;
	SmallVector<TemplateParamInfo, 0> TemplateParameters;
	
	CommentEntry Documentation;
	std::string DocumentationGroup;
};

struct ClassInfo : GeneratedTypeInformation
{
	int ClassFlags = 0;

	std::vector<MethodInfo> Constructors;
	std::vector<PropertyInfo> Properties;
	std::vector<MethodInfo> Methods;
	std::vector<MethodInfo> Events;
	std::vector<FieldInfo> Fields;

	/** Scans the class information for a constructor that is not already used, and return the signature of the first such constructor. */
	MethodInfo FindUnusedConstructorSignature() const;

	/** Checks is the provided flag set on the class. */
	bool IsFlagSet(enum ClassFlags flag) const { return (ClassFlags & (int)flag) != 0; }
};

struct ExternalClassInfos
{
	std::vector<MethodInfo> Methods;
};

struct SimpleConstructorInfo
{
	std::vector<VariableInformation> Parameters;
	std::unordered_map<std::string, std::string> FieldAssignments; /**< Maps which class/struct field maps to which constructor parameter, by name. */
	CommentEntry Documentation;
};

struct StructInfo : GeneratedTypeInformation
{
	std::string InteropName;

	std::vector<SimpleConstructorInfo> Constructors;
	std::vector<FieldInfo> Fields;
	bool RequiresInteropType = false;
	bool IsTemplateInstatiation = false;
};

/** Information about a single entry within an enum. */
struct EnumEntryInfo
{
	std::string NativeName;
	std::string ScriptName;
	std::string Value;
	CommentEntry Documentation;
};

struct EnumInfo : GeneratedTypeInformation
{
	std::string ScriptName;

	std::string ExplicitUnderlyingCSharpType; /**< Explicit underlying type of the enum, as a C# type. */
	std::unordered_map<int, EnumEntryInfo> Entries;
};

struct ForwardDeclInfo
{
	ForwardDeclInfo() = default;
	ForwardDeclInfo(const std::string& typeName, const SmallVector<std::string, 4>& nameSpace, const SmallVector<TemplateParamInfo, 0>& templateParameters = {}, bool isStruct = false)
		:TypeName(typeName), Namespace(nameSpace), TemplateParameters(templateParameters), IsStruct(isStruct)
	{ }

	bool operator==(const ForwardDeclInfo& rhs) const
	{
		return TypeName == rhs.TypeName && Namespace == rhs.Namespace;
	}

	std::string TypeName;
	SmallVector<std::string, 4> Namespace;
	SmallVector<TemplateParamInfo, 0> TemplateParameters;
	bool IsStruct = false;
};

template<>
struct std::hash<ForwardDeclInfo>
{
	std::size_t operator()(const ForwardDeclInfo& value) const
	{
		std::hash<std::string> hasher;
		size_t hash = hasher(value.TypeName);

		for (auto& entry : value.Namespace)
			hash = hash_combine(hash, hasher(entry));
		
		return hash;
	}
};

struct FileInfo
{
	std::vector<ClassInfo> Classes;
	std::vector<StructInfo> Structs;
	std::vector<EnumInfo> Enums;

	std::unordered_set<ForwardDeclInfo> ForwardDeclarations;
	std::vector<std::string> ReferencedHeaderIncludes;
	std::vector<std::string> ReferencedSourceIncludes;
	bool InEditor = false;
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

inline bool IsAPIEditor(ApiFlags api)
{
	return ((int)api & (int)ApiFlags::Editor) != 0;
}

inline bool IsAPIEngine(ApiFlags api)
{
	return ((int)api & (int)ApiFlags::Engine) != 0;
}

inline bool IsAPIFramework(ApiFlags api)
{
	return ((int)api & (int)ApiFlags::Framework) != 0;
}

/** Determines if the provided API is usable depending on whether we're building the editor scripting or not. */
inline bool IsAPIValid(ApiFlags api, bool editor)
{
   return (editor && IsAPIEditor(api)) || (!editor && (IsAPIEngine(api) || IsAPIFramework(api)));
}

/** Contains a map of native types to script types. The key is the native name as provided in ClassInfo.Name, StructInfo.Name or EnumInfo.Name. */
extern std::unordered_map<std::string, ExternalClassInfos> externalClassInfos; // TODO - Move to TypeLookup
extern std::unordered_map<std::string, BaseClassInfo> baseClassLookup; // TODO - Move to TypeLookup

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

void GenerateCpp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode);
void GenerateCSharp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode);