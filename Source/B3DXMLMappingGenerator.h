#pragma once
#include "B3DCommon.h"

/** Handles generation of XML mapping that may be used to map native types to script types by external code. */
class XMLMappingGenerator
{
public:
	/**
	 * Generates an Info.xml file that maps all script types to native types, as well as provides their documentation.
	 *
	 * @param editor		True if generating code for the editor.
	 * @param outputFolder	Folder in which to place the XML file.
	 */
	static void GenerateMappingXMLFile(bool editor, const std::string& outputFolder);

private:
	/**
	 * Generates mapping information for a parameter.
	 *
	 * @param varInfo		Information about the parameter.
	 * @param methodDoc		Documentation for the method containing the parameter.
	 * @param indent		Indent level of the generated code.
	 * @return				Generated XML entry.
	 */
	static std::string GenerateXMLParamInfo(const VariableInformation& varInfo, const CommentEntry& methodDoc, const std::string& indent);

	/**
	 * Generates mapping information for a field.
	 *
	 * @param fieldInfo		Information about the field.
	 * @param indent		Indent level of the generated code.
	 * @return				Generated XML entry.
	 */
	static std::string GenerateXMLFieldInfo(const FieldInfo& fieldInfo, const std::string& indent);

	/**
	 * Generates mapping information for a method or constructor.
	 *
	 * @param methodInfo	Information about the method or constructor.
	 * @param indent		Indent level of the generated code.
	 * @param ctor			True if the method represents a constructor, rather than a regular method.
	 * @return				Generated XML entry.
	 */
	static std::string GenerateXMLMethodInfo(const MethodInfo& methodInfo, const std::string& indent, bool ctor);

	/**
	 * Generates mapping information for a constructor.
	 *
	 * @param methodInfo	Information about the constructor.
	 * @param indent		Indent level of the generated code.
	 * @return				Generated XML entry.
	 */
	static std::string GenerateXMLMethodInfo(const SimpleConstructorInfo& methodInfo, const std::string& indent);

	/**
	 * Generates mapping information for a property.
	 *
	 * @param propertyInfo		Information about the property.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLPropertyInfo(const PropertyInfo& propertyInfo, const std::string& indent);

	/**
	 * Generates mapping information for an event.
	 *
	 * @param eventInfo			Information about the event.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLEventInfo(const MethodInfo& eventInfo, const std::string& indent);

	/**
	 * Generates mapping information for an enum.
	 *
	 * @param input				Information about the enum.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLEnum(const EnumInfo& input, const std::string& indent);

	/**
	 * Generates mapping information for a struct.
	 *
	 * @param input				Information about the struct.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLStruct(const StructInfo& input, const std::string& indent);

	/**
	 * Generates mapping information for a class.
	 *
	 * @param input				Information about the class.
	 * @param editor			True if generating code for the editor.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLClass(const ClassInfo& input, bool editor, const std::string& indent);
};
