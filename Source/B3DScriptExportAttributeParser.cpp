#include "B3DScriptExportAttributeParser.h"
#include "B3DParserUtility.h"

bool ScriptExportAttributeParser::IsExportAttribute(AnnotateAttr* attr)
{
	StringRef annotation = attr->getAnnotation();

	return annotation.startswith("se,");
}

bool ScriptExportAttributeParser::IsExportable(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	AnnotateAttr *const attr = decl->getAttr<AnnotateAttr>();
	if (attr != nullptr)
		return IsExportAttribute(attr);

	return false;
}

std::string ScriptExportAttributeParser::FindExportableBaseClassName(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return "";

	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			std::string className = baseDecl->getName().str();

			if(ParserUtility::IsBuiltinBaseType(baseDecl))
			{
				iter++;
				continue;
			}

			if (IsExportable(baseDecl))
			{
				StringRef sourceClassName = baseDecl->getName();
				return sourceClassName.str();
			}

			todo.push(baseDecl);
			iter++;
		}
	}

	return "";
}

std::string ScriptExportAttributeParser::FindExportableBasePlainClassName(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return "";

	auto iter = decl->bases_begin();
	while (iter != decl->bases_end())
	{
		const CXXBaseSpecifier* baseSpec = iter;
		CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

		std::string className = baseDecl->getName().str();

		AnnotateAttr* attr = baseDecl->getAttr<AnnotateAttr>();
		if (attr != nullptr)
		{
			StringRef sourceClassName = baseDecl->getName();
			ScriptExportInformation parsedDeclInfo;

			if (ParseExportAttribute(attr, sourceClassName, parsedDeclInfo))
			{
				if((parsedDeclInfo.ExportFlags & (int)ExportFlags::ExportAsStruct) != 0)
					return sourceClassName.str();
			}
		}

		iter++;
	}

	return "";
}

void ScriptExportAttributeParser::ParseScriptExportAttributeCommand(const std::string& name, const std::string& value, StringRef typeName, ScriptExportInformation& output)
{
	auto fnParseBoolean = [&name, &typeName](const std::string& value, bool defaultValue) -> bool{
		if (value == "true") return true;
		if (value == "false") return false;

		outs() << "Warning: Invalid boolean value provided for script export option \"" << name << "\" on type " << typeName << ". Provided value: " << value << ".\n";
		return defaultValue;
	};

	auto fnParseVisibility = [&typeName](const std::string& value, CSVisibility defaultValue)
	{
		if (value == "Public") return CSVisibility::Public;
		if (value == "Private") return CSVisibility::Private;
		if (value == "Internal") return CSVisibility::Internal;

		outs() << "Warning: Invalid value provided for script export option \"Visbility\" on type " << typeName << ". Provided value: " << value << ".\n";
		return defaultValue;
	};

	auto fnParseAPI = [&typeName](const std::string& value, ExportFlags defaultValue)
	{
		if (value == "Framework") return ExportFlags::FrameworkAPI;
		if (value == "Engine") return ExportFlags::EngineAPI;
		if (value == "Editor") return ExportFlags::EditorAPI;

		outs() << "Warning: Invalid value provided for script export option \"API\" on type " << typeName << ". Provided value: " << value << ".\n";
		return defaultValue;
	};

	auto fnParseProperty = [&typeName](const std::string& value, ExportFlags defaultValue)
	{
		if (value == "Getter") return ExportFlags::PropertyGetter;
		if (value == "Setter") return ExportFlags::PropertySetter;

		outs() << "Warning: Invalid value provided for script export option \"Property\" on type " << typeName << ". Provided value: " << value << ".\n";
		return defaultValue;
	};

	if (name == "ExportName")
	{
		output.ExportedTypeName = value;
	}
	else if (name == "Visibility")
	{
		output.Visibility = fnParseVisibility(value, CSVisibility::Public);
	}
	else if (name == "ExportFile")
	{
		output.ExportedFileName = value;
	}
	else if (name == "ExportAsStruct")
	{
		if (fnParseBoolean(value, false)) 
			output.SetExportFlag(ExportFlags::ExportAsStruct);
	}
	else if (name == "Property")
	{
		output.SetExportFlag(fnParseProperty(value, ExportFlags::None));
	}
	else if (name == "API")
	{
		output.SetExportFlag(fnParseAPI(value, ExportFlags::None));
	}
	else if (name == "ExtensionMethodForType")
	{
		output.SetExportFlag(ExportFlags::ExternalMethod);
		output.ExtensionOfType = value;
	}
	else if (name == "ExtensionClassForType")
	{
		output.SetExportFlag(ExportFlags::ExternalClass);
		output.ExtensionOfType = value;
	}
	else if (name == "ExtensionConstructorForType")
	{
		output.SetExportFlag(ExportFlags::ExternalConstructor);
		output.ExtensionOfType = value;
	}
	else if (name == "Exclude")
	{
		if (fnParseBoolean(value, false)) 
			output.SetExportFlag(ExportFlags::Exclude);
	}
	else if (name == "InteropOnly")
	{
		if (fnParseBoolean(value, false)) 
			output.SetExportFlag(ExportFlags::InteropOnly);
	}
	else if (name == "DocumentationGroup")
	{
		output.DocumentationGroup = value;
	}
	else if(name == "Static")
	{
		output.SetExportFlag(ExportFlags::StaticClass);
	}
	else if(name == "Singleton")
	{
		output.SetExportFlag(ExportFlags::SingletonClass);
		output.SingletonGetterName = value;
	}
	else if (name == "UI")
	{
		if (value == "Hide")
		{
			output.MetaData.SetFlag(MetaDataFlags::ForceHideInInspector);
		}
		else if (value == "Show")
		{
			output.MetaData.SetFlag(MetaDataFlags::ForceShowInInspector);
		}
		else if (value == "AsLayerMask")
		{
			output.MetaData.SetFlag(MetaDataFlags::ShowAsLayerMask);
		}
		else if (value == "AsSlider")
		{
			output.MetaData.SetFlag(MetaDataFlags::ShowAsSlider);
		}
		else if (value == "IsHDRColor")
		{
			output.MetaData.SetFlag(MetaDataFlags::HDR);
		}
		else if (value == "AsQuaternion")
		{
			output.MetaData.SetFlag(MetaDataFlags::AsQuaternion);
		}
		else if (value == "Inline")
		{
			output.MetaData.SetFlag(MetaDataFlags::Inline);
		}
		else
		{
			outs() << "Warning: Invalid value provided for script export option \"UI\" on type " << typeName << ". Provided value: " << value << ".\n";
		}
	}
	else if (name == "PassByCopy")
	{
		if (fnParseBoolean(value, false)) 
			output.MetaData.SetFlag(MetaDataFlags::PassByCopy);
	}
	else if (name == "ApplyOnDirty")
	{
		if (fnParseBoolean(value, false)) 
			output.MetaData.SetFlag(MetaDataFlags::ApplyOnDirty);
	}
	else if (name == "LoadOnAssign")
	{
		if (fnParseBoolean(value, false)) 
			output.MetaData.SetFlag(MetaDataFlags::LoadOnAssign);
	}
	else if (name == "NotNullable")
	{
		if (fnParseBoolean(value, false)) 
			output.MetaData.SetFlag(MetaDataFlags::NotNull);
	}
	else if (name == "UIIncrementStep")
	{
		if (value.empty())
		{
			outs() << "Warning: Empty value provided for script export option \"" << name << "\" on type \"" << typeName << "\".\n";
		}
		else
		{
			output.MetaData.SetFlag(MetaDataFlags::Step);
			output.MetaData.IncrementStep = (float)atof(value.c_str());
		}
	}
	else if (name == "UIValueRange")
	{
		if (value.empty())
		{
			outs() << "Warning: Empty value provided for script export option \"" << name << "\" on type \"" << typeName << "\".\n";
		}
		else
		{
			const char* valueCharacters = value.c_str();
			const char* currentCharacter = valueCharacters;
			if(valueCharacters[0] != '[')
			{
				outs() << "Warning: Invalid value provided for script export option \"" << name << "\" on type \"" << typeName << "\". Value: " << value << "\n";
				return;
			}

			currentCharacter++;

			char* nextCharacter = nullptr;
			float firstValue = std::strtof(currentCharacter, &nextCharacter);
			currentCharacter = nextCharacter;

			float secondValue = 0.0f;

			bool isSecondValueFound = false;
			while(currentCharacter != '\0')
			{
				if (*currentCharacter == ',')
				{
					currentCharacter++;

					if (currentCharacter == '\0')
						break;

					secondValue = std::strtof(currentCharacter, &nextCharacter);
					currentCharacter = nextCharacter;

					isSecondValueFound = true;
					break;
				}

				currentCharacter++;
			}

			if (!isSecondValueFound)
			{
				outs() << "Warning: Invalid number of arguments provided for script export options \"" << name << "\" on type \"" << typeName << "\".\n";
			}
			else
			{
				if(*currentCharacter != ']')
				{
					outs() << "Warning: Missing closing ] in value provided for script export option \"" << name << "\" on type \"" << typeName << "\". Value: " << value << "\n";
				}

				output.MetaData.SetFlag(MetaDataFlags::Range);
				output.MetaData.RangeMinimum = firstValue;
				output.MetaData.RangeMaximum = secondValue;
			}
		}
	}
	else if (name == "UIOrder")
	{
		if (value.empty())
		{
			outs() << "Warning: Empty value provided for script export option \"" << name << "\" on type \"" << typeName << "\".\n";
		}
		else
		{
			output.MetaData.Flags |= (int)MetaDataFlags::Order;
			output.MetaData.UIOrder = atoi(value.c_str());
		}
	}
	else if (name == "UICategory")
	{
		if (value.empty())
		{
			outs() << "Warning: Empty value provided for script export option \"" << name << "\" on type \"" << typeName << "\".\n";
		}
		else
		{
			output.MetaData.Flags |= (int)MetaDataFlags::Category;
			output.MetaData.UICategory = value;
		}
	}
	else
	{
		outs() << "Warning: Unrecognized script export option \"" << name << "\" on type \"" << typeName << "\".\n";
	}
}

bool ScriptExportAttributeParser::ParseExportAttribute(AnnotateAttr* attr, StringRef sourceName, ScriptExportInformation& output)
{
	if(!IsExportAttribute(attr))
		return false;

	StringRef annotation = attr->getAnnotation();

	output.ExportedFileName = sourceName.str();
	output.Visibility = CSVisibility::Public;
	output.ExportFlags = 0;

	std::stringstream parameterName;
	std::stringstream parameterValue;

	bool foundParameterName = false;
	for (auto iter = annotation.begin() + 3; iter != annotation.end(); ++iter)
	{
		if (*iter == ' ' || *iter == '\t')
			continue;

		// End parsing value
		if (*iter == ')')
		{
			if (foundParameterName)
			{
				ParseScriptExportAttributeCommand(parameterName.str(), parameterValue.str(), sourceName, output);

				parameterName.str("");
				parameterName.clear();

				parameterValue.str("");
				parameterValue.clear();

				foundParameterName = false;
			}
			else
				errs() << "Error: Script export option parse error. Found ')' while parsing name.";

			continue;
		}

		if (*iter == ',')
		{
			if (foundParameterName)
			{
				parameterValue << ",";
			}

			continue;
		}

		// Start parsing value
		if (*iter == '(')
		{
			if (foundParameterName)
				errs() << "Error: Script export option parse error. Found '(' while parsing value.";
			else
				foundParameterName = true;

			continue;
		}

		if (!foundParameterName)
			parameterName << *iter;
		else
			parameterValue << *iter;
	}

	if(!parameterName.str().empty())
		ParseScriptExportAttributeCommand(parameterName.str(), parameterValue.str(), sourceName, output);

	return true;
}

bool ScriptExportAttributeParser::ParseExportAttribute(Decl* decl, StringRef sourceName, ScriptExportInformation& output)
{
	for (const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (ParseExportAttribute(entry, sourceName, output))
			return true;
	}

	return false;
}
