//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
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
	 * @param parameterInfo		Information about the parameter.
	 * @param methodComment		Documentation for the method containing the parameter.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLParamInfo(const VariableInformation& parameterInfo, const CommentEntry& methodComment, const std::string& indent);

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
	 * @param isConstructor	True if the method represents a constructor, rather than a regular method.
	 * @param indent		Indent level of the generated code.
	 * @return				Generated XML entry.
	 */
	static std::string GenerateXMLMethodInfo(const MethodInfo& methodInfo, bool isConstructor, const std::string& indent);

	/**
	 * Generates mapping information for a constructor.
	 *
	 * @param constructorInfo	Information about the constructor.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLMethodInfo(const StructConstructorInfo& constructorInfo, const std::string& indent);

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
	 * @param enumInfo			Information about the enum.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLEnum(const EnumInfo& enumInfo, const std::string& indent);

	/**
	 * Generates mapping information for a struct.
	 *
	 * @param structInfo		Information about the struct.
	 * @param indent			Indent level of the generated code.
	 * @return					Generated XML entry.
	 */
	static std::string GenerateXMLStruct(const StructInfo& structInfo, const std::string& indent);

	/**
	 * Generates mapping information for a class.
	 *
	 * @param classInfo					Information about the class.
	 * @param isGeneratingEditorCode	True if generating code for the editor.
	 * @param indent					Indent level of the generated code.
	 * @return							Generated XML entry.
	 */
	static std::string GenerateXMLClass(const ClassInfo& classInfo, bool isGeneratingEditorCode, const std::string& indent);
};
