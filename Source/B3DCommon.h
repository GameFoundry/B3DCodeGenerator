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

extern const char *const kBuiltinGameObjectType;
extern const char *const kBuiltinComponentType;
extern const char *const kBuiltinSceneObjectType;
extern const char *const kBuiltinResourceType;
extern const char *const kBuiltinModuleType;
extern const char *const kBuiltinGUIElementType;
extern const char *const kBuiltinReflectableType;
extern const char* const kBuiltinIScriptExportableType;

extern std::string sFrameworkCppNs;
extern std::string sEditorCppNs;
extern std::string sFrameworkCsNs;
extern std::string sEditorCsNs;
extern std::string sFrameworkDllExportMacro;
extern std::string sEditorDllExportMacro;
extern std::string sFrameworkCopyrightNotice;
extern std::string sEditorCopyrightNotice;

/** C# visibility of a declaration. */
enum class CSVisibility
{
	Public,
	Internal,
	Private
};

/** Determines for which API we're exporting a declaration. */
enum class ApiFlags
{
	Framework = 1 << 0, /**< Declaration is only part of the framework API. */
	Engine = 1 << 1, /**< Declaration is only part of the engine API. */
	Editor = 1 << 2, /**< Declaration is only part of the editor API. */
	Any = Framework | Engine /**< Declaration is part of both the framework and the engine API (most common case). */
};

/** Determines which meta-data to set for a member. @see MemberMetaData */
enum class MetaDataFlags
{
	ForceHideInInspector = 1 << 0,		/**< Property or field will be hidden in the inspector. When neither ForceHideInInspector nor ForceShowInInspector is provided, defaults are used depending on if the property/field is public and it's type. */
	ForceShowInInspector = 1 << 1,		/**< Property of field will be shown in the inspector. When neither ForceHideInInspector nor ForceShowInInspector is provided, defaults are used depending on if the property/field is public and it's type. */
	ShowAsSlider = 1 << 2,		/**< An integer or floating point value will be displayed as a slider, rather than an input box. */
	ShowAsLayerMask = 1 << 3,	/**< An integer value will be displayed as a multi-selection layer mask drop down rather than an input box. */
	Range = 1 << 4,			/**< An integer or floating point value will be limited to a specific range. Exact range is controlled via ExportStyle. */
	Step = 1 << 5,			/**< An integer or floating point value can only be incremented using a specific step. Exact step is controlled via ExportStyle. */
	Category = 1 << 6,		/**< Places the field or property in a specific sub-category in the inspector. Exact category is specified in ExportStyle. */
	Order = 1 << 7,			/**< Determines an explicit order a field should be displayed in the inspector. Exact order is specified in ExportStyle. */
	NotNull = 1 << 8,		/**< Specifies that the field or property should not be allowed to have a null value. */
	PassByCopy = 1 << 9,	/**< Property or field passes its value by copy. This lets the inspector know to re-assign the value to the field/property if any of its child values changes. */
	ApplyOnDirty = 1 << 10, /**< Similar to PassByCopy, although the value is not passed by copy, but the system still requires you to re-assign the field/property if any child value changes, as internal dirty flags need to be set. */
	AsQuaternion = 1 << 11, /**< Forces a quaternion to be displayed as a quaternion. By default it will be displayed using euler angles. */
	Inline = 1 << 12,		/**< Displays members as if they were part of the parent class. */
	LoadOnAssign = 1 << 13, /**< Loads the resource when it is assigned to the field. */
	HDR = 1 << 14,			/**< Displays a Color value as an HDR color value. */
};

/** Meta-data to be generated when the member is exported to script. In C# this usually translates to attributes. */
struct MemberMetaData
{
	float RangeMinimum = 0.0f;
	float RangeMaximum = 0.0f;
	float IncrementStep = 0.0f;
	int UIOrder = 0;
	std::string UICategory;
	int Flags = 0;

	void SetFlag(enum MetaDataFlags flag) { Flags |= (int)flag; }
};

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

/** Base type for describing all variable, parameter, return value or field  declarations. */
struct VariableBase
{
	VariableTypeInformation TypeInformation; /**< Information about the variable type. */
};

/** Information about a variable or a parameter. */
struct VariableInformation : VariableBase
{
	std::string Name; /**< Name of the variable. */

	std::string DefaultValue; /**< Default value to assign to the variable. This will be a literal such as 5.0f, 5, true or "" if @p DefaultValueType is empty, or comma separated parameters to pass to the type constructor if not. */
	std::string DefaultValueType; /**< Type of value to construct in case the default value is a constructible type. Empty if default value is a literal.*/
};

/** Information about a return value. */
struct ReturnInfo : VariableBase
{ };


/** Information about a field. */
struct FieldInfo : VariableInformation
{
	CommentEntry Documentation; /**< Documentation comments for the field. */
	MemberMetaData MetaData; /**< Additional meta-data to associate with the field. */
};

/** Information about a template parameter. */
struct TemplateParamInfo
{
	std::string TypeName;
};

/** Flags that describe how is a method exported. */
enum class MethodFlags
{
	Static = 1 << 0,			/**< Method is static. */
	External = 1 << 1,			/**< Method is defined in a different native class. */
	Constructor = 1 << 2,		/**< Method is a constructor. */
	PropertyGetter = 1 << 3,	/**< Method is a getter for a property. Must have no parameters and a return value of the correct type. */
	PropertySetter = 1 << 4,	/**< Method is a setter for a property. Must have no return value and a single parameter of the correct type. */
	InteropOnly = 1 << 5,		/**< Only the script interop method will be generated, but no public means to invoke it. */
	Callback = 1 << 6,			/**< Method is a native callback. Only a script partial method will be generated, and it's up to the user to implement it. */
	FieldWrapper = 1 << 7,		/**< Method wraps access to a native field. */
	CSOnly = 1 << 8,			/**< Only the C# version of this method, without C# interop method, and any C++ interop. This can be useful as in some cases exported methods can share the same interop code and this avoids it being generated multiple times. */
};

/** Information about a method. */
struct MethodInfo
{
	std::string NativeName; /**< Native name of the original method in C++. */
	std::string InteropName; /**< Name of the interop wrapper method in C++. */
	std::string ScriptName; /**< Name of the method in C#. */
	CSVisibility Visibility = CSVisibility::Public; /**< Visibility of the method in the class. */
	ApiFlags API = ApiFlags::Framework; /**< Determines for which APIs is the method exported. */

	ReturnInfo ReturnValue; /**< Return value, if any. If void, the type information will be empty. */
	std::vector<VariableInformation> Parameters; /**< Input parameters for the method, if any. */
	CommentEntry Documentation; /**< Documentation comments for the method. */

	std::string ExternalClass; /**< If this method is defined on an external class, this specifies which external class defines the native method. */
	int MethodFlags = 0; /**< Flags that control how is the method exported. */
	MemberMetaData MetaData; /**< Additional meta-data to associate with the method. */

	/** Checks is the provided flag set on the method. */
	bool IsFlagSet(enum MethodFlags flag) const { return (MethodFlags & (int)flag) != 0; }
};

/** Information about a property. */
struct PropertyInfo
{
	VariableTypeInformation TypeInformation; /**< Information about the type of the property. */
	std::string ScriptName; /**< Name of the property in C#. */

	std::string GetterName; /**< Name of the getter method in C++. */
	std::string SetterName; /**< Name of the setter method in C++. */

	CSVisibility Visibility = CSVisibility::Public; /**< Visibility of the property in the class. */
	ApiFlags API = ApiFlags::Framework; /**< Determines for which APIs is the property exported. */
	bool IsStatic = false; /**< True if the property is static. */
	MemberMetaData MetaData; /**< Additional meta-data to associate with the property. */
	CommentEntry Documentation; /**< Documentation comments for the property. */
};

/** Base class for all exported types. */
struct GeneratedTypeInformation
{
	std::string NativeName; /**< Native name of the type. */
	std::string BaseClassName; /**< Base class of the type, if any. */
	SmallVector<std::string, 4> Namespace; /**< Namespace the native type is defined in. */

	CSVisibility Visibility = CSVisibility::Public; /**< Visibility of the type. */
	ApiFlags API = ApiFlags::Framework; /**< Determines for which APIs is the type exported. */

	std::string NativeNameWithoutTemplateArguments; /**< Native name of the type, with template parameters stripped. */
	SmallVector<TemplateParamInfo, 0> TemplateParameters; /**< Template parameters of the native type, if any. */
	
	CommentEntry Documentation; /**< Documentation comments for the type. */
	std::string DocumentationGroup; /**< Documentation group in which to place the generated type. */
};

/** Flags that describe how is a class exported. */
enum class ClassFlags
{
	IsBase = 1 << 0,						/**< Class represents a base class. */
	IsModule = 1 << 1,						/**< Class is a module and only has a single instance. */
	IsTemplateInst = 1 << 2,				/**< Class is an instance of a template. */
	IsStruct = 1 << 3,						/**< Class is defined as a struct in native code. */
	HideInInspector = 1 << 4,				/**< Class members will be hidden in the inspector. */
	UsesIScriptExportableAPI = 1 << 5		/**< Class derives from IScriptExportable and uses the new script export code. */
};

/** Information about a generated class. */
struct ClassInfo : GeneratedTypeInformation
{
	int ClassFlags = 0; /**< Assigned flags, of ClassFlags type. */

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

/** Contains a list of methods to inject into a script class, that were not originally defined in their native class. */
struct ExternalClassInfos
{
	std::vector<MethodInfo> Methods;
};

/** Contains information about a constructor of a struct. */
struct StructConstructorInfo
{
	std::string StaticMethodName; /**< If provided, the constructor will be created as a static method that returns object of the type. */
	std::vector<VariableInformation> Parameters; /**< Parameters to the constructor. */
	std::unordered_map<std::string, std::string> FieldAssignments; /**< Maps which class/struct field maps to which constructor parameter, by name. */
	CommentEntry Documentation; /**< Documentation comments for the constructor. */
};

/** Information about a generated struct. */
struct StructInfo : GeneratedTypeInformation
{
	std::string InteropName; /**< Name of the struct in the interop code. This will be the same as native struct name if interop type is not required. */

	std::vector<StructConstructorInfo> Constructors;
	std::vector<FieldInfo> Fields;
	bool RequiresInteropType = false; /**< If true, the struct contains some types that cannot be memcpyed into script, and requires an intermediate struct to which we first to the translation from native, and then memcpy the intermediate struct. */
	bool IsTemplateInstatiation = false; /**< If true, struct is a template instantiation. */
};

/** Information about a single entry within an enum. */
struct EnumEntryInfo
{
	std::string NativeName; /**< Name of the enum entry in native code. */
	std::string ScriptName; /**< Name of the enum entry in script code. */
	std::string Value; /**< Value of the enum entry. */
	CommentEntry Documentation; /**< Documentation comments for the enum entry. */
};

/** Information about a generated enum. */
struct EnumInfo : GeneratedTypeInformation
{
	std::string ScriptName; /**< Name of the enum in script code. */

	std::string ExplicitUnderlyingCSharpType; /**< Explicit underlying type of the enum, as a C# type. */
	std::unordered_map<int, EnumEntryInfo> Entries; /**< Enum entries. */
};

/** Information about a forward declaration required for a specific type. */
struct ForwardDeclarationInformation
{
	ForwardDeclarationInformation() = default;
	ForwardDeclarationInformation(const std::string& typeName, const SmallVector<std::string, 4>& nameSpace, const SmallVector<TemplateParamInfo, 0>& templateParameters = {}, bool isStruct = false)
		:TypeName(typeName), Namespace(nameSpace), TemplateParameters(templateParameters), IsStruct(isStruct)
	{ }

	bool operator==(const ForwardDeclarationInformation& rhs) const
	{
		return TypeName == rhs.TypeName && Namespace == rhs.Namespace;
	}

	std::string TypeName; /**< Name of the type. */
	SmallVector<std::string, 4> Namespace; /**< Namespace the type is in. */
	SmallVector<TemplateParamInfo, 0> TemplateParameters; /**< Template parameters for the type, if any. */
	bool IsStruct = false; /**< True if the type is a struct, false if it a class. */
};

template<>
struct std::hash<ForwardDeclarationInformation>
{
	std::size_t operator()(const ForwardDeclarationInformation& value) const
	{
		std::hash<std::string> hasher;
		size_t hash = hasher(value.TypeName);

		for (auto& entry : value.Namespace)
			hash = hash_combine(hash, hasher(entry));
		
		return hash;
	}
};

/** Contains information about all entries required for generating a single script file, and a matching .h/.cpp set of interop files. */
struct FileInfo
{
	std::vector<ClassInfo> Classes; /**< Classes exported in the file. */
	std::vector<StructInfo> Structs; /**< Structs exported in the file. */
	std::vector<EnumInfo> Enums; /**< Enums exported in the file. */

	std::unordered_set<ForwardDeclarationInformation> ForwardDeclarations; /**< Forward declarations that need to be specified in the interop header file. */
	std::vector<std::string> ReferencedHeaderIncludes; /**< Includes that need to be specified in the interop .h file. */
	std::vector<std::string> ReferencedSourceIncludes; /**< Includes that need to be specified in the interop .cpp file. */
	bool InEditor = false; /**< True if the file is being generated for the editor API. */
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
	std::string cleanName = name;
	std::replace(cleanName.begin(), cleanName.end(), '<', '_');
	std::replace(cleanName.begin(), cleanName.end(), '>', '_');

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
