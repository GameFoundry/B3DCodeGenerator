#include "B3DXMLMappingGenerator.h"
#include "B3DXMLCommentGenerator.h"
#include "B3DGeneratorUtility.h"
#include "B3DTypeLookup.h"

std::string XMLMappingGenerator::GenerateXMLParamInfo(const VariableInformation& parameterInfo, const CommentEntry& methodComment, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<param name=\"" << GeneratorUtility::EscapeXML(parameterInfo.Name) << "\" type=\"" <<
		GeneratorUtility::EscapeXML(TypeLookup::GetNativeToScriptTypeMapping(parameterInfo.TypeInformation).ScriptTypeName) << "\">\n";

	auto iterFind = std::find_if(methodComment.ParameterComments.begin(), methodComment.ParameterComments.end(),
		[&varName = parameterInfo.Name](const CommentParameterEntry& entry) { return varName == entry.Name; });
	if (iterFind != methodComment.ParameterComments.end() && !iterFind->Comments.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(iterFind->Comments) << "</doc>\n";

	output << indent << "</param>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLFieldInfo(const FieldInfo& fieldInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<field name=\"" << GeneratorUtility::EscapeXML(fieldInfo.Name) << "\" type=\"" <<
		GeneratorUtility::EscapeXML(TypeLookup::GetNativeToScriptTypeMapping(fieldInfo.TypeInformation).ScriptTypeName) << "\">\n";

	// TODO - Generate inspector visibility
	if(!fieldInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(fieldInfo.Documentation.Brief) << "</doc>\n";

	output << indent << "</field>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLMethodInfo(const MethodInfo& methodInfo, bool isConstructor, const std::string& indent)
{
	std::stringstream output;

   std::string isStaticStr = "false";
   bool isStatic = methodInfo.IsFlagSet(MethodFlags::Static);
   if(!isConstructor && isStatic)
	   isStaticStr = "true";

	if(!isConstructor)
	{
		output << indent << "<method native=\"" << GeneratorUtility::EscapeXML(methodInfo.NativeName) << "\" script=\"" <<
			GeneratorUtility::EscapeXML(methodInfo.ScriptName) << "\" static=\"" << isStaticStr << "\">\n";
	}
	else
		output << indent << "<ctor>\n";

	if(!methodInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(methodInfo.Documentation.Brief) << "</doc>\n";

	for(auto& param : methodInfo.Parameters)
		output << GenerateXMLParamInfo(param, methodInfo.Documentation, indent + "\t");

	if(!isConstructor && !methodInfo.ReturnValue.TypeInformation.IsEmpty())
	{
		output << indent << "\t<returns type=\"" << GeneratorUtility::EscapeXML(TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ReturnValue.TypeInformation).ScriptTypeName) << "\">\n";

		if (!methodInfo.Documentation.ReturnValueComments.empty())
			output << indent << "\t\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(methodInfo.Documentation.ReturnValueComments) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	if(!isConstructor)
		output << indent << "</method>\n";
	else
		output << indent << "</ctor>\n";

	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLMethodInfo(const StructConstructorInfo& constructorInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<ctor>\n";
	if(!constructorInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(constructorInfo.Documentation.Brief) << "</doc>\n";

	for(auto& param : constructorInfo.Parameters)
		output << GenerateXMLParamInfo(param, constructorInfo.Documentation, indent + "\t");

	output << indent << "</ctor>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLPropertyInfo(const PropertyInfo& propertyInfo, const std::string& indent)
{
	std::string staticStr = propertyInfo.IsStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<property name=\"" << GeneratorUtility::EscapeXML(propertyInfo.ScriptName) << "\" type=\"" <<
		GeneratorUtility::EscapeXML(TypeLookup::GetNativeToScriptTypeMapping(propertyInfo.TypeInformation).ScriptTypeName) <<
		"\" getter=\"" << GeneratorUtility::EscapeXML(propertyInfo.GetterName) << "\" setter=\"" << GeneratorUtility::EscapeXML(propertyInfo.SetterName) <<
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if(!propertyInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(propertyInfo.Documentation.Brief) << "</doc>\n";

	output << indent << "</property>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLEventInfo(const MethodInfo& eventInfo, const std::string& indent)
{
   bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
   std::string staticStr = isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<event native=\"" << GeneratorUtility::EscapeXML(eventInfo.NativeName) << "\" script=\"" << GeneratorUtility::EscapeXML(eventInfo.ScriptName) <<
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if (!eventInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(eventInfo.Documentation.Brief) << "</doc>\n";

	for(auto& param : eventInfo.Parameters)
		output << GenerateXMLParamInfo(param, eventInfo.Documentation, indent + "\t");

	if(!eventInfo.ReturnValue.TypeInformation.IsEmpty())
	{
		output << indent << "\t<returns type=\"" << GeneratorUtility::EscapeXML(TypeLookup::GetNativeToScriptTypeMapping(eventInfo.ReturnValue.TypeInformation).ScriptTypeName) << "\">\n";

		if (!eventInfo.Documentation.ReturnValueComments.empty())
			output << indent << "\t\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(eventInfo.Documentation.ReturnValueComments) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	output << indent << "</event>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLEnum(const EnumInfo& enumInfo, const std::string& indent)
{
	std::stringstream output;

	output << indent << "<enum native=\"" << GeneratorUtility::EscapeXML(enumInfo.NativeName) << "\" script=\"" << GeneratorUtility::EscapeXML(enumInfo.ScriptName) << "\">\n";
	if (!enumInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(enumInfo.Documentation.Brief) << "</doc>\n";

	for (auto I = enumInfo.Entries.begin(); I != enumInfo.Entries.end(); ++I)
	{
		const EnumEntryInfo& entryInfo = I->second;

	   output << indent << "\t<enumentry native=\"" << GeneratorUtility::EscapeXML(entryInfo.NativeName) << "\" script=\"" << GeneratorUtility::EscapeXML(entryInfo.ScriptName) << "\">\n";
	   if (!entryInfo.Documentation.Brief.empty())
		   output << indent << "\t\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(entryInfo.Documentation.Brief) << "</doc>\n";
	   output << indent << "\t</enumentry>\n";
	}

	output << indent << "</enum>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLStruct(const StructInfo& structInfo, const std::string& indent)
{
	std::stringstream output;

	const TypeMappingInformation& typeInfo = TypeLookup::GetNativeToScriptTypeMapping(structInfo.NativeName);

	output << indent << "<struct native=\"" << GeneratorUtility::EscapeXML(structInfo.NativeName) << "\" script=\"" << GeneratorUtility::EscapeXML(typeInfo.ScriptTypeName) << "\">\n";
	if (!structInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(structInfo.Documentation.Brief) << "</doc>\n";

	for (auto& entry : structInfo.Constructors)
		output << GenerateXMLMethodInfo(entry, indent + "\t");

	for(auto& entry : structInfo.Fields)
	  output << GenerateXMLFieldInfo(entry, indent + "\t");

	output << indent << "</struct>\n";
	return output.str();
}

std::string XMLMappingGenerator::GenerateXMLClass(const ClassInfo& classInfo, bool isGeneratingEditorCode, const std::string& indent)
{
	std::stringstream output;

	const TypeMappingInformation& typeInfo = TypeLookup::GetNativeToScriptTypeMapping(classInfo.NativeName);

	output << indent << "<class native=\"" << GeneratorUtility::EscapeXML(classInfo.NativeName) << "\" script=\"" << GeneratorUtility::EscapeXML(typeInfo.ScriptTypeName) << "\">\n";
	if (!classInfo.Documentation.Brief.empty())
		output << indent << "\t<doc>" << XMLCommentGenerator::GenerateXMLCommentParagraph(classInfo.Documentation.Brief) << "</doc>\n";

	for (auto& entry : classInfo.Constructors)
	{
		bool interopOnly = entry.IsFlagSet(MethodFlags::InteropOnly);
		if (IsAPIValid(entry.API, isGeneratingEditorCode) && !interopOnly)
			output << GenerateXMLMethodInfo(entry, true, indent + "\t");
	}

	for(auto& entry : classInfo.Methods)
	{
		const bool interopOnly = entry.IsFlagSet(MethodFlags::InteropOnly);
		const bool isConstructor = entry.IsFlagSet(MethodFlags::Constructor);
		const bool isProperty = entry.IsFlagSet(MethodFlags::PropertyGetter) || entry.IsFlagSet(MethodFlags::PropertySetter);

		if (IsAPIValid(entry.API, isGeneratingEditorCode) && !interopOnly && !isProperty)
			output << GenerateXMLMethodInfo(entry, isConstructor, indent + "\t");
	}

   for(auto& entry : classInfo.Properties)
   {
		if(IsAPIValid(entry.API, isGeneratingEditorCode))
			output << GenerateXMLPropertyInfo(entry, indent + "\t");
   }

   for(auto& entry : classInfo.Events)
   {
	   bool isCallback = entry.IsFlagSet(MethodFlags::Callback);
	   bool isInternal = entry.IsFlagSet(MethodFlags::InteropOnly);

	  if(!isCallback && !isInternal)
		  output << GenerateXMLEventInfo(entry, indent + "\t");
   }

	output << indent << "</class>\n";
	return output.str();
}

void XMLMappingGenerator::GenerateMappingXMLFile(bool editor, const std::string& outputFolder)
{
	std::stringstream body;
	for (const auto& fileInfo : TypeLookup::GetFilesToGenerate())
	{
		auto& enumInfos = fileInfo.second.Enums;
		for (const auto& entry : enumInfos)
		{
			if (IsAPIValid(entry.API, editor))
				body << GenerateXMLEnum(entry, "\t");
		}

		auto& structInfos = fileInfo.second.Structs;
		for (const auto& entry : structInfos)
		{
			if (IsAPIValid(entry.API, editor))
				body << GenerateXMLStruct(entry, "\t");
		}


		auto& classInfos = fileInfo.second.Classes;
		for (const auto& entry : classInfos)
		{
			if (IsAPIValid(entry.API, editor))
				body << GenerateXMLClass(entry, editor, "\t");
		}
	}

	std::ofstream output = GeneratorUtility::CreateFile("info.xml", outputFolder);

	output << "<?xml version='1.0' encoding='UTF-8' standalone='no'?>\n";
	output << "<entries>\n";
	output << body.str();
	output << "</entries>\n";
	output.close();
}

