#include "B3DParserUtility.h"
#include "B3DCommentParser.h"
#include "B3DTypeLookup.h"

std::string ParserUtility::GetNamespace(const NamedDecl* decl)
{
	if (decl == nullptr)
		return std::string();

	const DeclContext* context = decl->getDeclContext();

	// Collect contexts.
	SmallVector<const DeclContext *, 8> contexts;
	while (context && isa<NamedDecl>(context))
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	std::string name;
	raw_string_ostream ss(name);
	for (const DeclContext* declContext : reverse(contexts))
	{
		if (const auto *ND = dyn_cast<NamespaceDecl>(declContext))
		{
			if (!ND->isAnonymousNamespace())
				ss << *ND << "::";
		}
	}

	return ss.str();
}

std::string ParserUtility::GetFullName(const NamedDecl* decl)
{
	if (decl == nullptr)
		return std::string();

	const DeclContext* context = decl->getDeclContext();

	// Collect contexts.
	SmallVector<const DeclContext *, 8> contexts;
	while (context && isa<NamedDecl>(context)) 
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	std::string name;
	raw_string_ostream ss(name);
	for (const DeclContext* declContext : reverse(contexts))
	{
		if (const auto *ND = dyn_cast<NamespaceDecl>(declContext))
		{
			if (ND->isAnonymousNamespace())
				ss << "(anonymous namespace)";
			else
				ss << *ND;
		}
		else if (const auto *RD = dyn_cast<RecordDecl>(declContext))
		{
			if (!RD->getIdentifier())
				ss << "(anonymous " << RD->getKindName() << ')';
			else
				ss << *RD;
		}
		else if (const auto *ED = dyn_cast<EnumDecl>(declContext))
		{
			if (ED->isScoped() || ED->getIdentifier())
				ss << *ED;
			else
				continue;
		}
		else
			ss << *cast<NamedDecl>(declContext);

		ss << "::";
	}

	if (decl->getDeclName() || isa<DecompositionDecl>(decl))
		ss << *decl;
	else
		ss << "(anonymous)";

	return ss.str();
}

bool ParserUtility::CheckIsBuiltinModuleType(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return false;

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
			if (className == kBuiltinModuleType)
				return true;

			todo.push(baseDecl);
			iter++;
		}
	}

	return false;
}

bool ParserUtility::IsBuiltinBaseType(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	if (className == kBuiltinComponentType)
		return true;
	else if (className == kBuiltinResourceType)
		return true;
	else if (className == kBuiltinSceneObjectType)
		return true;
	else if (className == kBuiltinModuleType)
		return true;
	else if (className == kBuiltinGUIElementType)
		return true;
	else if (className == kBuiltinReflectableType)
		return true;

	return false;
}

ApiFlags ParserUtility::ParseAPIFromExportFlags(int exportFlags)
{
	int output = 0;

	if((exportFlags & (int)ExportFlags::EngineAPI) != 0)
		output |= (int)ApiFlags::Engine;

	if((exportFlags & (int)ExportFlags::FrameworkAPI) != 0)
		output |= (int)ApiFlags::Framework;

	if((exportFlags & (int)ExportFlags::EditorAPI) != 0)
		output |= (int)ApiFlags::Editor;

	if((int)output == 0)
		output = (int)ApiFlags::Any;

	return (ApiFlags)output;
}

bool ParserUtility::MapBuiltinPrimitiveTypeToCppType(BuiltinType::Kind kind, std::string& output)
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
		output = "char";
		return true;
	case BuiltinType::SChar:
		output = "int8_t";
		return true;
	case BuiltinType::Char_U:
		output = "uint8_t";
		return true;
	case BuiltinType::Short:
		output = "int16_t";
		return true;
	case BuiltinType::Int:
		output = "int32_t";
		return true;
	case BuiltinType::Long:
		output = "int32_t";
		return true;
	case BuiltinType::LongLong:
		output = "int64_t";
		return true;
	case BuiltinType::UChar:
		output = "uint8_t";
		return true;
	case BuiltinType::UShort:
		output = "uint16_t";
		return true;
	case BuiltinType::UInt:
		output = "uint32_t";
		return true;
	case BuiltinType::ULong:
		output = "uint32_t";
		return true;
	case BuiltinType::ULongLong:
		output = "uint64_t";
		return true;
	case BuiltinType::Float:
		output = "float";
		return true;
	case BuiltinType::Double:
		output = "double";
		return true;
	case BuiltinType::WChar_S:
	case BuiltinType::WChar_U:
		output = "wchar_t";
		return true;
	case BuiltinType::Char16:
		output = "char16_t";
		return true;
	case BuiltinType::Char32:
		output = "char32_t";
		return true;
	default:
		break;
	}

	errs() << "Unrecognized builtin type found.\n";
	return false;
}

bool ScriptExportUtility::IsExportAttribute(AnnotateAttr* attr)
{
	StringRef annotation = attr->getAnnotation();

	return annotation.startswith("se,");
}

bool ScriptExportUtility::IsExportable(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	AnnotateAttr *const attr = decl->getAttr<AnnotateAttr>();
	if (attr != nullptr)
		return IsExportAttribute(attr);

	return false;
}

std::string ScriptExportUtility::FindExportableBaseClassName(const CXXRecordDecl* decl)
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

std::string ScriptExportUtility::FindExportableBasePlainClassName(const CXXRecordDecl* decl)
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

void ScriptExportUtility::ParseScriptExportAttributeCommand(const std::string& name, const std::string& value, StringRef typeName, ScriptExportInformation& output)
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
	else if (name == "UI")
	{
		if (value == "Hide")
		{
			output.Style.SetFlag(StyleFlags::ForceHide);
		}
		else if (value == "Show")
		{
			output.Style.SetFlag(StyleFlags::ForceShow);
		}
		else if (value == "AsLayerMask")
		{
			output.Style.SetFlag(StyleFlags::AsLayerMask);
		}
		else if (value == "AsSlider")
		{
			output.Style.SetFlag(StyleFlags::AsSlider);
		}
		else if (value == "IsHDRColor")
		{
			output.Style.SetFlag(StyleFlags::HDR);
		}
		else if (value == "AsQuaternion")
		{
			output.Style.SetFlag(StyleFlags::AsQuaternion);
		}
		else if (value == "Inline")
		{
			output.Style.SetFlag(StyleFlags::Inline);
		}
		else
		{
			outs() << "Warning: Invalid value provided for script export option \"UI\" on type " << typeName << ". Provided value: " << value << ".\n";
		}
	}
	else if (name == "PassByCopy")
	{
		if (fnParseBoolean(value, false)) 
			output.Style.SetFlag(StyleFlags::PassByCopy);
	}
	else if (name == "ApplyOnDirty")
	{
		if (fnParseBoolean(value, false)) 
			output.Style.SetFlag(StyleFlags::ApplyOnDirty);
	}
	else if (name == "LoadOnAssign")
	{
		if (fnParseBoolean(value, false)) 
			output.Style.SetFlag(StyleFlags::LoadOnAssign);
	}
	else if (name == "NotNullable")
	{
		if (fnParseBoolean(value, false)) 
			output.Style.SetFlag(StyleFlags::NotNull);
	}
	else if (name == "UIIncrementStep")
	{
		if (value.empty())
		{
			outs() << "Warning: Empty value provided for script export option \"" << name << "\" on type \"" << typeName << "\".\n";
		}
		else
		{
			output.Style.SetFlag(StyleFlags::Step);
			output.Style.IncrementStep = (float)atof(value.c_str());
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

				output.Style.SetFlag(StyleFlags::Range);
				output.Style.RangeMinimum = firstValue;
				output.Style.RangeMaximum = secondValue;
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
			output.Style.StyleFlags |= (int)StyleFlags::Order;
			output.Style.UIOrder = atoi(value.c_str());
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
			output.Style.StyleFlags |= (int)StyleFlags::Category;
			output.Style.UICategory = value;
		}
	}
	else
	{
		outs() << "Warning: Unrecognized script export option \"" << name << "\" on type \"" << typeName << "\".\n";
	}
}

bool ScriptExportUtility::ParseExportAttribute(AnnotateAttr* attr, StringRef sourceName, ScriptExportInformation& output)
{
	if(!IsExportAttribute(attr))
		return false;

	StringRef annotation = attr->getAnnotation();

	output.ExportedTypeName = sourceName.str();
	
	if (!output.ExportedTypeName.empty())
	{
		// Camel case to pascal case
		if(islower(output.ExportedTypeName[0]))
			output.ExportedTypeName[0] = toupper(output.ExportedTypeName[0]);
		else
		{
			// Screaming snake case to pascal case
			bool isScreamingSnakeCase = true;
			std::stringstream caseOutput;
			bool nextUpper = true;
			for(size_t i = 0; i < output.ExportedTypeName.size(); i++)
			{
				if (isalpha(output.ExportedTypeName[i]))
				{
					if(islower(output.ExportedTypeName[i]))
					{
						isScreamingSnakeCase = false;
						break;
					}
					else
					{
						if(!nextUpper)
							caseOutput << (char)tolower(output.ExportedTypeName[i]);
						else
						{
							caseOutput << output.ExportedTypeName[i];
							nextUpper = false;
						}
					}
				}
				else if(output.ExportedTypeName[i] == '_')
					nextUpper = true;
				else
					caseOutput << output.ExportedTypeName[i];
			}

			if(isScreamingSnakeCase)
				output.ExportedTypeName = caseOutput.str();
		}
	}

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

bool ScriptExportUtility::ParseExportAttribute(Decl* decl, StringRef sourceName, ScriptExportInformation& output)
{
	for (const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (ParseExportAttribute(entry, sourceName, output))
			return true;
	}

	return false;
}
