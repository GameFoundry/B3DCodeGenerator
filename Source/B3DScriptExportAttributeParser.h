#pragma once
#include "B3DCommon.h"

enum class ExportFlags
{
	None = 0,
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

/** Information used for controlling script export of a particular declaration. */
struct ScriptExportInformation
{
	std::string ExportedTypeName; /**< Name to export as to scripting. */
	std::string ExportedFileName; /**< Explicit file to export to. */
	std::string ExtensionOfType; /**< If the declaration is acting as an extension for some other class, this tells us for which class is the declaration an extension of. */
	std::string DocumentationGroup; /**< Name of the documentation group to wrap the generated code in. */
	CSVisibility Visibility = CSVisibility::Public; /**< Visibility of the declaration in generated code. */
	int ExportFlags = 0; /**< Various flags controlling export. */
	ExportStyle Style; /**< Additional settings controlling how is the exported declaration API generated. */

	void SetExportFlag(enum ExportFlags flag) { ExportFlags |= (int)flag; }
};

/** Utility functionality for script export attribute parsing. */
class ScriptExportAttributeParser
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

private:
	/**
	 * Parses a command from a script export attribute string.
	 *
	 * @param		name			Name of the command.
	 * @param		value			Value assigned to the command.
	 * @param		typeName		Name of the type we're parsing the command for. Used for error reporting.
	 * @param[out]	output			Information structure that will appended with the parsed information.
	 */
	static void ParseScriptExportAttributeCommand(const std::string& name, const std::string& value, StringRef typeName, ScriptExportInformation& output);
};