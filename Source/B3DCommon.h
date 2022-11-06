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

#include "B3DVariableTypeInformation.h"

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

/** C# visibility of a declaration. */
enum class CSVisibility
{
	Public,
	Internal,
	Private
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
	float RangeMinimum = 0.0f;
	float RangeMaximum = 0.0f;
	float IncrementStep = 0.0f;
	int UIOrder = 0;
	std::string UICategory;
	int StyleFlags = 0;

	void SetFlag(enum StyleFlags flag) { StyleFlags |= (int)flag; }
};

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
struct CommentParagraph
{
	std::string Text;
	SmallVector<CommentReference, 2> ParameterReferences; /**< Locations within @p text at which method parameters are referenced. Only relevant if the current comment is part of a method comment. */
	SmallVector<CommentReference, 2> DeclarationReferences; /**< Locations within @p text at which other declarations are referenced (e.g. other types, methods, fields). */
};

/** Contains zero or multiple paragraphs of comment text for a method parameter. */
struct CommentParameterEntry
{
	std::string Name; /**< Name of the parameter that's being commented. */
	SmallVector<CommentParagraph, 2> Comments; /**< Zero or multiple comment text paragraphs. */
};

/** Contains comment text for a particular type, method or field. */
struct CommentEntry
{
	SmallVector<CommentParagraph, 2> Brief; /**< Summary comment describing the type, method or field. */

	SmallVector<CommentParameterEntry, 4> ParameterComments; /**< Comments for method parameters, if this is a method comment. */
	SmallVector<CommentParagraph, 2> ReturnValueComments; /**< Zero or multiple comment paragraphs describing method return value, if this is a method comment. */
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

/** Flags that describe how is a method exported. */
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

/** Flags that describe how is a class exported. */
enum class ClassFlags
{
	IsBase = 1 << 0,
	IsModule = 1 << 1,
	IsTemplateInst = 1 << 2,
	IsStruct = 1 << 3,
	HideInInspector = 1 << 4
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

/**
 * Generates all C++ generated files.
 *
 * @param engineOutputFolder			Folder to output engine/framework files.
 * @param editorOutputFolder			Folder to output editor-specific files.
 * @param generateEditorCode			If true, we're generating code for the editor, rather than the engine/framework.
 */
void GenerateCpp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode);

/**
 * Generates all C# generated files.
 *
 * @param engineOutputFolder			Folder to output engine/framework files.
 * @param editorOutputFolder			Folder to output editor-specific files.
 * @param generateEditorCode			If true, we're generating code for the editor, rather than the engine/framework.
 */
void GenerateCSharp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode);