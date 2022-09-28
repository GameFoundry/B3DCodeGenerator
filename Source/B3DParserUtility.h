#pragma once
#include "B3DCommon.h"

class CommentParser;

/** Offers various utility functionality useful for parsing. */
class ParserUtility
{
public:
	/** Returns the namespace of the provided declaration. */
	static std::string GetNamespace(const NamedDecl* decl);

	/** Returns the fully qualified name (namespace + type) of the provided declaration. */
	static std::string GetFullName(const NamedDecl* decl);

	/** Checks is the provided declaration a Banshee::Module type. */
	static bool CheckIsBuiltinModuleType(const CXXRecordDecl* decl);

	/** Returns true if the provided declaration is one of the builtin base types (e.g. component, resource, IReflectable). */
	static bool IsBuiltinBaseType(const CXXRecordDecl* decl);

	static void PostProcessFileInfos(CommentParser& commentParser);
private:
	/** Splits a method with default parameters into multiple methods, if some of the parameter default values cannot be parsed. */
	static void PostProcessDefaultParameters(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos);

	static void GatherIncludes(const std::string& typeName, int flags, bool isEditor, IncludesInfo& output);
	static void GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output);
	static void GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output);
	static void GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output);
	static void GatherIncludes(const StructInfo& structInfo, IncludesInfo& output);
};

/** Information used for controlling script export of a particular declaration. */
struct ScriptExportInformation
{
	std::string ExportedTypeName; /**< Name to export as to scripting. */
	std::string ExportedFileName; /**< Explicit file to export to. */
	std::string ExtensionOfType; /**< If the declaration is acting as an extension for some other class, this tells us for which class is the declaration an extension of. */
	std::string DocumentationGroup; /**< Name of the documentation group to wrap the generated code in. */
	CSVisibility Visibility = CSVisibility::Public; /**< Visibility of the declaration in generated code. */
	int ExportFlags = 0; /**< Various flags controlling export. */
	ExportStyle style; /**< Additional settings controlling how is the exported declaration API generated. */
};

/** Utility functionality for script export parsing. */
class ScriptExportUtility // TODO: Move to its own file
{
public:
	/** Checks is the provided attribute a script export attribute. */
	static bool IsExportAttribute(AnnotateAttr* attr);

	/** Checks if the provided declaration has the script export attribute. */
	static bool IsExportable(const CXXRecordDecl* decl);

	/** Returns the name of the first base class of the provided declaration that has the script export attribute. */
	static std::string FindExportableBaseClassName(const CXXRecordDecl* decl);

	/** Returns the name of the first base class of the provided declaration that has the script export attribute with the ExportAsStruct option set. */
	static std::string FindExportableBasePlainClassName(const CXXRecordDecl* decl);

	/**
	 * Parses a command from a script export attribute string.
	 *
	 * @param		name			Name of the command.
	 * @param		value			Value assigned to the command.
	 * @param		sourceName		Name of the type we're parsing the command for. Used for error reporting.
	 * @param[out]	output			Information structure that will appended with the parsed information.
	 */
	static void ParseScriptExportAttributeCommand(const std::string& name, const std::string& value, StringRef sourceName, ScriptExportInformation& output);

	/**
	 * Parses all commands from the provided script export attribute.
	 *
	 * @param		attr			Attribute to parse the script export commands from.
	 * @param		sourceName		Name of the source type we're parsing script export commands for. Used for error reporting.
	 * @param[out]	output			Information structure that contains information about all parsed commands.
	 * @return						True if the provided attribute was a script export attribute, false otherwise.
	 */
	static bool ParseExportAttribute(AnnotateAttr* attr, StringRef sourceName, ScriptExportInformation& output);

	/**
	 * Parses all attributes from the provided declaration looking for the script export attribute. If founds, its commands are parsed.
	 *
	 * @param		decl			Declaration to scan for script export attributes.
	 * @param		sourceName		Name of the source type we're parsing script export commands for. Used for error reporting.
	 * @param[out]	output			Information structure that contains information about all parsed commands from the script export attribute.
	 * @return						True if the script export attribute was found, false otherwise.
	 */
	static bool ParseExportAttribute(Decl* decl, StringRef sourceName, ScriptExportInformation& output);
};