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

/** Determines the high level type of the exported class/struct. */
enum class TypeCategory
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

// TODO - Refactor type parsing to output this struct instead, replace uses of TypeFlags
// - Move all the getters that check flags here
struct TypeInformation
{
	// Type: Primitive, Vector, Array, SmallVector, AsyncOp, Path, ComponentOrActor, MonoObject, String, WString, ResourceHandle, GameObjectHandle, Shared, FlagsEnum
	// Type name (Direct type for Primitive, Path, MonoObject, String, WString, underlying type for ComponentOrActor, ResourceHandle, GameObjectHandle, FlagsEnum)
	// Optional<TypeInformation>: Underlying Type (For Vector, Array, SmallVector, Shared, AsyncOp)
	// Native qualifiers: Pointer, Reference, Const

	// PostProcessTypeFlags: IsReferencingComplexStruct, IsReferencingBaseClass
	// ParameterFlags: VarParams, AsResourceRef
};

enum class TypeFlags // TODO - Ideally this is split up into types and qualifiers
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

	TypeMappingInformation(SmallVector<std::string, 4> nativeNamespace, const std::string& scriptName, ::TypeCategory typeCategory, const std::string& nativeFile, const std::string& destFile)
		:NativeNamespace(std::move(nativeNamespace)), ScriptTypeName(scriptName), TypeCategory(typeCategory), NativeFile(nativeFile), InteropFile(destFile), EditorInteropFile(destFile)
	{ }

	TypeMappingInformation(SmallVector<std::string, 4> nativeNamespace, const std::string& scriptName, ::TypeCategory typeCategory, const std::string& nativeFile, const std::string& destFile,
		const std::string& destFileEditor)
		:NativeNamespace(std::move(nativeNamespace)), ScriptTypeName(scriptName), TypeCategory(typeCategory), NativeFile(nativeFile), InteropFile(destFile), EditorInteropFile(destFileEditor)
	{ }

	std::string ScriptTypeName; /**< Name of the type in the script code. */
	SmallVector<std::string, 4> NativeNamespace; /**< Namespace in which the native type is located in. Used for e.g. forward declares in generated native interop code. */
	std::string NativeFile; /**< File in which the native type is defined in. Used for resolving includes. */
	std::string InteropFile; /**< File in which the interop for this type is defined in. Used for resolving includes. */
	std::string EditorInteropFile; /**< Same as @p InteropFile, but if a type is exported in both framework and editor, then we need to generate two interop files. */
	::TypeCategory TypeCategory; /**< Determines a high level category that this type belongs to. */
	BuiltinType::Kind EnumUnderlyingType; /**< Underlying primitive type for enum or enum class. */
};

struct VarTypeInfo
{
	std::string typeName;
	unsigned arraySize;
	int flags;
};

struct VarInfo : VarTypeInfo
{
	std::string name;

	std::string defaultValue;
	std::string defaultValueType;
};

struct ReturnInfo : VarTypeInfo
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

struct FieldInfo : VarInfo
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
	std::vector<VarInfo> paramInfos;
	CommentEntry documentation;

	std::string externalClass;
	int flags;
	ExportStyle style;
};

struct PropertyInfo
{
	std::string name;
	std::string type;

	std::string getter;
	std::string setter;

	CSVisibility visibility;
	ApiFlags api;
	int typeFlags;
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
	std::vector<VarInfo> params;
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

enum IncludeType
{
	IT_HEADER = 1 << 0,
	IT_IMPL = 1 << 1,
	IT_FWD = 1 << 2,
	IT_FWD_AND_IMPL = IT_FWD | IT_IMPL
};

struct IncludeInfo
{
	IncludeInfo() { }
	IncludeInfo(const std::string& typeName, const TypeMappingInformation& typeInfo, uint32_t originIncludeFlags, 
		uint32_t interopIncludeFlags, bool isStruct = false, bool isEditor = false)
		: typeName(typeName), typeInfo(typeInfo), originIncludeFlags(originIncludeFlags)
		, interopIncludeFlags(interopIncludeFlags), isStruct(isStruct), isEditor(isEditor)
	{ }

	std::string typeName;
	TypeMappingInformation typeInfo;
	uint32_t originIncludeFlags;
	uint32_t interopIncludeFlags;
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

/** Contains a map of native types to script types. The key is the native name as provided in ClassInfo.Name, StructInfo.Name or EnumInfo.Name. */
extern std::unordered_map<std::string, TypeMappingInformation> NativeToScriptTypeMap;
extern std::unordered_map<std::string, FileInfo> outputFileInfos;
extern std::unordered_map<std::string, ExternalClassInfos> externalClassInfos;
extern std::unordered_map<std::string, BaseClassInfo> baseClassLookup;

inline StructInfo* findStructInfo(const std::string& name)
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

inline TypeMappingInformation getTypeInfo(const std::string& sourceType, int flags)
{
	if ((flags & (int)TypeFlags::Primitive) != 0)
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = mapCppTypeToCSType(sourceType);
		outType.TypeCategory = ::TypeCategory::Primitive;

		return outType;
	}

	if ((flags & (int)TypeFlags::String) != 0)
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ::TypeCategory::String;

		return outType;
	}

	if ((flags & (int)TypeFlags::WString) != 0)
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ::TypeCategory::WString;

		return outType;
	}

	if ((flags & (int)TypeFlags::Path) != 0)
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ::TypeCategory::Path;

		return outType;
	}

	if ((flags & (int)TypeFlags::MonoObject) != 0)
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "object";
		outType.TypeCategory = ::TypeCategory::MonoObject;

		return outType;
	}

	if ((flags & (int)TypeFlags::AsResourceRef) != 0)
	{
		TypeMappingInformation outType;

		if (sourceType == "Resource")
		{
			outType = NativeToScriptTypeMap.find("Resource")->second;
			outType.ScriptTypeName = "RRefBase";
		}
		else
		{
			auto iterFind = NativeToScriptTypeMap.find(sourceType);
			if (iterFind != NativeToScriptTypeMap.end())
			{
				outType = iterFind->second;
				outType.ScriptTypeName = "RRef<" + iterFind->second.ScriptTypeName + ">";
				assert(outType.type == ::ParsedType::Resource);
			}
			else
			{
				outType.ScriptTypeName = "RRefBase";
				outType.TypeCategory = ::TypeCategory::Resource;

				errs() << "Unable to map type \"" << sourceType << "\". Assuming generic resource.\n";
			}
		}

		if ((flags & (int)TypeFlags::AsyncOp) != 0)
			outType.ScriptTypeName = "AsyncOp<" + outType.ScriptTypeName + ">";

		return outType;
	}

	if ((flags & (int)TypeFlags::AsyncOp) != 0)
	{
		auto iterFind = NativeToScriptTypeMap.find(sourceType);
		if (iterFind != NativeToScriptTypeMap.end())
		{
			TypeMappingInformation outType = iterFind->second;
			outType.ScriptTypeName = "AsyncOp<" + iterFind->second.ScriptTypeName + ">";

			return outType;
		}
		else
		{
			TypeMappingInformation outType;
			outType.ScriptTypeName = "AsyncOp<" + sourceType + ">";
			outType.TypeCategory = ::TypeCategory::Class;

			errs() << "Unable to map type \"" << sourceType << "\". Assuming same name as source. \n";
			return outType;
		}
	}

	auto iterFind = NativeToScriptTypeMap.find(sourceType);
	if (iterFind == NativeToScriptTypeMap.end())
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = mapCppTypeToCSType(sourceType);
		outType.TypeCategory = ::TypeCategory::Primitive;

		errs() << "Unable to map type \"" << sourceType << "\". Assuming same name as source.\n";
		return outType;
	}

	return iterFind->second;
}

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

inline bool isInt64(const TypeMappingInformation& typeInfo)
{
	return typeInfo.TypeCategory == ::TypeCategory::Primitive && (typeInfo.ScriptTypeName == "long" || typeInfo.ScriptTypeName == "ulong");
}

inline bool isInteger(const TypeMappingInformation& typeInfo)
{
	return typeInfo.TypeCategory == ::TypeCategory::Primitive &&
		(typeInfo.ScriptTypeName == "int" || typeInfo.ScriptTypeName == "uint" ||
			typeInfo.ScriptTypeName == "long" || typeInfo.ScriptTypeName == "ulong" ||
			typeInfo.ScriptTypeName == "short" || typeInfo.ScriptTypeName == "ushort" ||
			typeInfo.ScriptTypeName == "byte");
}

inline bool isReal(const TypeMappingInformation& typeInfo)
{
	return typeInfo.TypeCategory == ::TypeCategory::Primitive &&
		(typeInfo.ScriptTypeName == "float" || typeInfo.ScriptTypeName == "double");
}

inline bool isOutput(int flags)
{
	return (flags & (int)TypeFlags::IsOutputQualifier) != 0;
}

inline bool isArray(int flags)
{
	return (flags & (int)TypeFlags::Array) != 0;
}

inline bool isVector(int flags)
{
	return (flags & (int)TypeFlags::Vector) != 0;
}

inline bool isSmallVector(int flags)
{
	return (flags & (int)TypeFlags::SmallVector) != 0;
}

inline bool isArrayOrVector(int flags)
{
	return (flags & ((int)TypeFlags::Vector | (int)TypeFlags::Array | (int)TypeFlags::SmallVector)) != 0;
}

inline bool isFlagsEnum(int flags)
{
	return (flags & (int)TypeFlags::FlagsEnum) != 0;
}

inline bool isSrcPointer(int flags)
{
	return (flags & (int)TypeFlags::IsNativePointerQualifier) != 0;
}

inline bool isSrcReference(int flags)
{
	return (flags & (int)TypeFlags::IsReferenceQualifier) != 0;
}

inline bool isSrcValue(int flags)
{
	int nonValueFlags = (int)TypeFlags::IsNativePointerQualifier | (int)TypeFlags::IsReferenceQualifier | (int)TypeFlags::IsSharedPointerQualifier |
		(int)TypeFlags::IsResourceHandleQualifier | (int)TypeFlags::IsGameObjectHandleQualifier;

	return (flags & nonValueFlags) == 0;
}

inline bool isSrcSPtr(int flags)
{
	return (flags & (int)TypeFlags::IsSharedPointerQualifier) != 0;
}

inline bool isSrcRHandle(int flags)
{
	return (flags & (int)TypeFlags::IsResourceHandleQualifier) != 0;
}

inline bool isSrcGHandle(int flags)
{
	return (flags & (int)TypeFlags::IsGameObjectHandleQualifier) != 0;
}

inline bool isComplexStruct(int flags)
{
	return (flags & (int)TypeFlags::IsStructWrapperUsed) != 0;
}

inline bool isBaseParam(int flags)
{
	return (flags & (int)TypeFlags::IsReferencingBaseClass) != 0;
}

inline bool isVarParam(int flags)
{
	return (flags & (int)TypeFlags::VarParams) != 0;
}

inline bool getPassAsResourceRef(int flags)
{
	return (flags & (int)TypeFlags::AsResourceRef) != 0;
}

inline bool getIsComponentOrActor(int flags)
{
	return (flags & (int)TypeFlags::ComponentOrActor) != 0;
}

inline bool getIsAsyncOp(int flags)
{
	return (flags & (int)TypeFlags::AsyncOp) != 0;
}

inline bool isStruct(int flags)
{
	return (flags & (int)ClassFlags::IsStruct) != 0;
}

inline bool isHandleType(::TypeCategory type)
{
	return type == ::TypeCategory::Resource || type == ::TypeCategory::SceneObject || type == ::TypeCategory::Component;
}

inline bool isClassType(::TypeCategory type)
{
	return type == ::TypeCategory::Class || type == ::TypeCategory::ReflectableClass;
}

inline bool isPlainStruct(::TypeCategory type, int flags)
{
	return type == ::TypeCategory::Struct && !isArrayOrVector(flags);
}

inline bool isPassedByValue(int flags)
{
	return (isSrcReference(flags) || isSrcValue(flags)) && !isSrcSPtr(flags) && !isSrcRHandle(flags) && !isSrcGHandle(flags);
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

inline bool willBeDereferenced(int flags)
{
	return (isSrcReference(flags) || isSrcValue(flags) || isSrcPointer(flags)) && !isSrcSPtr(flags) && !isSrcRHandle(flags) && !isSrcGHandle(flags);
}

inline bool needsIntermediateArray(::TypeCategory type, int flags = 0)
{
	if(type == ::TypeCategory::Class || type == ::TypeCategory::ReflectableClass)
		return !isSrcSPtr(flags);

	return false;
}

inline bool isCSOnly(int flags)
{
	return (flags & (int)MethodFlags::CSOnly) != 0;
}

inline bool canBeReturned(::TypeCategory type, int flags)
{
	if (isOutput(flags))
		return false;

	if (isArrayOrVector(flags))
		return true;

	if (type == ::TypeCategory::Struct)
		return false;

	return true;
}

inline bool endsWith(const std::string& str, const std::string& end) 
{
	if (str.length() >= end.length()) 
		return (0 == str.compare(str.length() - end.length(), end.length(), end));

	return false;
}

inline std::string cleanTemplParams(const std::string& name)
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

inline std::string getStructInteropType(const std::string& name)
{
	return "__" + cleanTemplParams(name) + "Interop";
}

inline bool isValidStructType(TypeMappingInformation& typeInfo, int flags)
{
	if (isOutput(flags))
		return false;

	return true;
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

void generateAll(StringRef cppEngineOutputFolder, StringRef cppEditorOutputFolder, StringRef csEngineOutputFolder, 
	StringRef csEditorOutputFolder, bool genEditor);