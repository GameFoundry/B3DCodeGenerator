#include "B3DParserUtility.h"
#include "B3DCommentParser.h"
#include "B3DScriptExportAttributeParser.h"
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

bool ParserUtility::HasIScriptExportableBaseClass(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return false;

	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* currentDeclaration = todo.top();
		todo.pop();

		auto it = currentDeclaration->bases_begin();
		while (it != currentDeclaration->bases_end())
		{
			const CXXBaseSpecifier* baseSpecifier = it;
			CXXRecordDecl* baseDeclaration = baseSpecifier->getType()->getAsCXXRecordDecl();

			std::string baseClassName = baseDeclaration->getName().str();
			if (baseClassName == kBuiltinIScriptExportableType)
				return true;

			todo.push(baseDeclaration);
			it++;
		}
	}

	return false;
}

bool ParserUtility::IsBuiltinBaseType(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	if(className == kBuiltinGameObjectType)
		return true;
	else if (className == kBuiltinComponentType)
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

std::string ParserUtility::ConvertToPascalCase(const std::string& name)
{
	std::string output = name;

	if (!output.empty())
	{
		// Camel case to pascal case
		if(islower(output[0]))
			output[0] = toupper(output[0]);
		else
		{
			// Screaming snake case to pascal case
			bool isScreamingSnakeCase = true;
			std::stringstream caseOutput;
			bool nextUpper = true;
			for(size_t i = 0; i < output.size(); i++)
			{
				if (isalpha(output[i]))
				{
					if(islower(output[i]))
					{
						isScreamingSnakeCase = false;
						break;
					}
					else
					{
						if(!nextUpper)
							caseOutput << (char)tolower(output[i]);
						else
						{
							caseOutput << output[i];
							nextUpper = false;
						}
					}
				}
				else if(output[i] == '_')
					nextUpper = true;
				else
					caseOutput << output[i];
			}

			if(isScreamingSnakeCase)
				output = caseOutput.str();
		}
	}

	return output;
}

std::string ParserUtility::ReplaceInvalidTypeNameCharacters(const std::string& name)
{
	std::string output = name;

	std::replace(output.begin(), output.end(), '<', '_');
	std::replace(output.begin(), output.end(), '>', '_');
	std::replace(output.begin(), output.end(), ',', '_');
	std::replace(output.begin(), output.end(), ' ', '_');

	return output;
}

