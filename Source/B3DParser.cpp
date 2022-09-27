#include "B3DParser.h"
#include <cctype>

#include "B3DParserUtility.h"

::ParsedType getObjectType(const CXXRecordDecl* decl)
{
	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		if (curDecl->hasDefinition())
		{
			auto iter = curDecl->bases_begin();
			while (iter != curDecl->bases_end())
			{
				const CXXBaseSpecifier* baseSpec = iter;
				CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

				std::string className = baseDecl->getName().str();

				if (className == BUILTIN_COMPONENT_TYPE)
					return ::ParsedType::Component;
				else if (className == BUILTIN_RESOURCE_TYPE)
					return ::ParsedType::Resource;
				else if (className == BUILTIN_SCENEOBJECT_TYPE)
					return ::ParsedType::SceneObject;
				else if (className == BUILTIN_GUIELEMENT_TYPE)
					return ::ParsedType::GUIElement;
				else if (className == BUILTIN_REFLECTABLE_TYPE)
					return ::ParsedType::ReflectableClass;

				todo.push(baseDecl);
				iter++;
			}
		}
	}

	return ::ParsedType::Class;
}

bool isGameObjectOrResource(QualType type)
{
	const RecordType* recordType = type->getAs<RecordType>();
	if (recordType == nullptr)
		return false;

	const RecordDecl* recordDecl = recordType->getDecl();
	const CXXRecordDecl* cxxDecl = dyn_cast<CXXRecordDecl>(recordDecl);
	if (cxxDecl == nullptr)
		return false;

	::ParsedType objType = getObjectType(cxxDecl);
	return objType == ::ParsedType::Component || objType == ::ParsedType::SceneObject || objType == ::ParsedType::Resource;
}

std::string getNamespace(const RecordDecl* decl)
{
	std::string nsName;
	const DeclContext* nsContext = decl->getEnclosingNamespaceContext();
	if (nsContext != nullptr && nsContext->isNamespace())
	{
		// Note: Not checking more than one level of namespaces
		const NamespaceDecl* nsDecl = cast<NamespaceDecl>(nsContext);

		nsName = nsDecl->getName().str();
	}

	return nsName;
}

void updateParamRefComments(const std::vector<VarInfo>& paramInfos, CommentText& comment)
{
	for(auto iter = comment.ParameterReferences.begin(); iter != comment.ParameterReferences.end();)
	{
		const CommentReference& entry = *iter;

		auto iterFind = std::find_if(paramInfos.begin(), paramInfos.end(), 
			[&entry](const VarInfo& varInfo)
		{
			return entry.Name == varInfo.name;
		});

		if (iterFind == paramInfos.end())
		{
			comment.TemplateParameterReferences.push_back(entry);
			iter = comment.ParameterReferences.erase(iter);
		}
		else
			++iter;
	}
}

void updateParamRefComments(const std::vector<VarInfo>& paramInfos, CommentEntry& comment)
{
	for (auto& entry : comment.brief)
		updateParamRefComments(paramInfos, entry);

	for (auto& entry : comment.params)
	{
		for(auto& textEntry : entry.comments)
			updateParamRefComments(paramInfos, textEntry);
	}

	for (auto& entry : comment.returns)
		updateParamRefComments(paramInfos, entry);
}

void clearParamRefComments(CommentEntry& comment)
{
	updateParamRefComments({}, comment);
}

void registerUserTypeInfo(const SmallVector<std::string, 4>& classNs, const std::string& className, ApiFlags api, 
	const std::string declFile, const std::string& exportName, const std::string& exportFile, ::ParsedType type)
{
	std::string destFile = "BsScript" + exportFile + ".generated.h";
	std::string destFileEditor = destFile;

	// Going to need separate file for editor?
	if (hasAPIBED(api) && hasAPIBSF(api))
		destFileEditor = "BsScript" + exportFile + ".editor.generated.h";

	cppToCsTypeMap[className] = UserTypeInfo(classNs, exportName, type, declFile, destFile, destFileEditor);
}

template<class T>
void addEntryToFile(FileInfo& fileInfo, T& entry, const std::string& file, std::function<void(FileInfo&, const T&)> addEntry)
{
	if (hasAPIBED(entry.api))
	{
		// Editor only file
		if(!hasAPIBSF(entry.api))
		{
			fileInfo.inEditor = true;
			addEntry(fileInfo, entry);
		}
		else // Editor and bsf, add new file for editor
		{
			entry.api = ApiFlags::BSF;
			addEntry(fileInfo, entry);

			entry.api = ApiFlags::BED;

			std::string editorFile = file + ".editor";

			FileInfo& editorFileInfo = outputFileInfos[editorFile];
			editorFileInfo.inEditor = true;
			addEntry(editorFileInfo, entry);
		}
	}
	else // Non-editor, bsf and/or b3d
		addEntry(fileInfo, entry);
}

bool BansheeCodeGeneratorASTVisitor::parseType(QualType type, VarTypeInfo& outType, bool returnValue)
{
	outType.flags = 0;
	outType.arraySize = 0;

	QualType realType;
	if (type->isPointerType())
	{
		realType = type->getPointeeType();
		outType.flags |= (int)TypeFlags::SrcPtr;

		if (!returnValue && !realType.isConstQualified())
			outType.flags |= (int)TypeFlags::Output;
	}
	else if (type->isReferenceType())
	{
		realType = type->getPointeeType();
		outType.flags |= (int)TypeFlags::SrcRef;

		if (!returnValue && !realType.isConstQualified())
			outType.flags |= (int)TypeFlags::Output;
	}
	else
		realType = type;

	// Check for arrays & core variant types
	if (realType->isStructureOrClassType())
	{
		// Note: Not supporting nested arrays
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();

		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = realType->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName().str();

			// Note: vector parsing code copied below
			if (sourceTypeName == "vector" && recordDecl->isInStdNamespace())
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::Vector;
			}
			else if(sourceTypeName == "SmallVector")
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::SmallVector;

				uint32_t smallVectorSize = 0;
				if(numArgs > 1)
				{
					std::string tmplArgExprValue, exprType;
					if (evaluateExpression(specType->getArg(1).getAsExpr(), tmplArgExprValue, exprType))
					{
						try
						{
							smallVectorSize = std::stoi(tmplArgExprValue);
						}
						catch(const std::invalid_argument& ex)
						{
							outs() << "Error: Cannot convert SmallVector size template argument to a number, ignoring it.\n";
						}
						catch(const std::out_of_range& ex)
						{
							outs() << "Error: Cannot convert SmallVector size template argument to a number, ignoring it.\n";
						}
						
					}
					else
						outs() << "Error: Template argument for SmallVector cannot be constantly evaluated, ignoring it.\n";
				}

				outType.arraySize = smallVectorSize;
			}
			else if(sourceTypeName == "ComponentOrActor")
			{
				bool foundUnderlying = false;
				const DeclContext* context = dyn_cast<DeclContext>(recordDecl);
				for (auto I = context->decls_begin(); I != context->decls_end(); ++I)
				{
					if (TypeAliasDecl* typeAliasDecl = dyn_cast<TypeAliasDecl>(*I))
					{
						if(typeAliasDecl->getName() == "HandleType")
						{
							realType = typeAliasDecl->getUnderlyingType();
							foundUnderlying = true;
							break;
						}
					}
				}

				if(!foundUnderlying)
				{
					outs() << "Error: Cannot find underlying component type for ComponentOrActor<T>.\n";
					return false;
				}

				outType.flags |= (int)TypeFlags::ComponentOrActor;
			}
			else if(sourceTypeName == "TAsyncOp")
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::AsyncOp;
			}
			else
			{
				const TemplateDecl* templateDecl = specType->getTemplateName().getAsTemplateDecl();
				if(templateDecl)
				{
					std::string templateDeclName = templateDecl->getName().str();
					if((templateDeclName == "CoreVariantType" || templateDeclName == "CoreVariantHandleType") && specType->isTypeAlias())
						realType = specType->getAliasedType();
				}
			}
		}
	}
	else if(realType->isArrayType())
	{
		const ConstantArrayType* arrayType = dyn_cast<ConstantArrayType>(astContext->getAsArrayType(realType));
		if (arrayType)
		{
			realType = arrayType->getElementType();

			outType.arraySize = (unsigned)arrayType->getSize().getZExtValue();
			outType.flags |= (int)TypeFlags::Array;
		}
	}

	// Check for non-array template types
	if (realType->isStructureOrClassType())
	{
		// Check for arrays & flags
		// Note: Not supporting nested arrays
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = realType->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName().str();

			if (sourceTypeName == "vector" && recordDecl->isInStdNamespace())
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::Vector;
			}
			else if(sourceTypeName == "SmallVector")
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::SmallVector;

				uint32_t smallVectorSize = 0;
				if(numArgs > 1)
				{
					std::string tmplArgExprValue, exprType;
					if (evaluateExpression(specType->getArg(1).getAsExpr(), tmplArgExprValue, exprType))
					{
						try
						{
							smallVectorSize = std::stoi(tmplArgExprValue);
						}
						catch(const std::invalid_argument& ex)
						{
							outs() << "Error: Cannot convert SmallVector size template argument to a number, ignoring it.\n";
						}
						catch(const std::out_of_range& ex)
						{
							outs() << "Error: Cannot convert SmallVector size template argument to a number, ignoring it.\n";
						}
						
					}
					else
						outs() << "Error: Template argument for SmallVector cannot be constantly evaluated, ignoring it.\n";
				}

				outType.arraySize = smallVectorSize;
			}

			if(sourceTypeName == "Flags")
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::FlagsEnum;

				if(numArgs > 1)
				{
					QualType storageType = specType->getArg(1).getAsType();
					bool validStorageType = false;
					if (storageType->isBuiltinType())
					{
						const BuiltinType* builtinType = realType->getAs<BuiltinType>();
						std::string storageTypeStr;
						if (mapBuiltinTypeToCppType(builtinType->getKind(), storageTypeStr))
						{
							if (storageTypeStr == "uint32_t")
								validStorageType = true;
						}

						if(!validStorageType)
						{
							outs() << "Error: Invalid storage type used for Flags.\n";
							return false;
						}
					}
				}
			}
			else if (sourceTypeName == "basic_string" && recordDecl->isInStdNamespace())
			{
				realType = specType->getArg(0).getAsType();

				const BuiltinType* builtinType = realType->getAs<BuiltinType>();
				if (builtinType->getKind() == BuiltinType::Kind::WChar_U ||
					builtinType->getKind() == BuiltinType::Kind::WChar_S)
				{
					outType.typeName = "WString";
					outType.flags |= (int)TypeFlags::WString;
				}
				else
				{
					outType.typeName = "String";
					outType.flags |= (int)TypeFlags::String;
				}

				return true;
			}
			else if (sourceTypeName == "shared_ptr" && recordDecl->isInStdNamespace())
			{
				outType.flags |= (int)TypeFlags::SrcSPtr;

				realType = specType->getArg(0).getAsType();
				if (isGameObjectOrResource(realType))
				{
					outs() << "Error: Game object and resource types are only allowed to be referenced through handles"
						<< " for scripting purposes\n";

					return false;
				}
			}
			else if (sourceTypeName == "TResourceHandle")
			{
				// Note: Not supporting weak resource handles

				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::SrcRHandle;
				outType.flags |= (int)TypeFlags::AsResourceRef;
			}
			else if (sourceTypeName == "GameObjectHandle")
			{
				realType = specType->getArg(0).getAsType();
				outType.flags |= (int)TypeFlags::SrcGHandle;
			}
		}
	}

	if (realType->isPointerType())
	{
		outs() << "Error: Only normal pointers are supported for parameter types.\n";
		return false;
	}

	if (realType->isBuiltinType())
	{
		const BuiltinType* builtinType = realType->getAs<BuiltinType>();
		if (!mapBuiltinTypeToCppType(builtinType->getKind(), outType.typeName))
			return false;

		outType.flags |= (int)TypeFlags::Builtin;
		return true;
	}
	else if (realType->isStructureOrClassType())
	{
		const RecordType* recordType = realType->getAs<RecordType>();
		const RecordDecl* recordDecl = recordType->getDecl();

		std::string sourceTypeName = recordDecl->getName().str();

		// Handle special templated types
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
		if (specType != nullptr)
		{
			int numArgs = specType->getNumArgs();
			if (numArgs > 0)
			{
				// Handle generic template specializations
				sourceTypeName += parseTemplArguments(sourceTypeName, specType->getArgs(), specType->getNumArgs(), nullptr);
			}
		}
		else
		{
			// Check for a direct pointer to a managed object
			if(sourceTypeName == "_MonoObject")
			{
				if (isSrcPointer(outType.flags))
					outType.flags |= (int)TypeFlags::MonoObject;
				else
				{
					outs() << "Error: Found an object of type MonoObject but not passed by pointer. This is not supported. \n";
					return false;
				}
			}
			else if(sourceTypeName == "Path")
			{
				outType.flags |= (int)TypeFlags::Path;
			}
			else if (sourceTypeName == "StringID")
			{
				outType.flags |= (int)TypeFlags::String;
			}
		}

		// Its a user-defined type
		outType.typeName = sourceTypeName;
		return true;
	}
	else if (realType->isEnumeralType())
	{
		const EnumType* enumType = realType->getAs<EnumType>();
		const EnumDecl* enumDecl = enumType->getDecl();

		std::string sourceTypeName = enumDecl->getName().str();
		outType.typeName = sourceTypeName;
		return true;
	}
	else
	{
		outs() << "Error: Unrecognized type\n";
		return false;
	}
}

struct FunctionTypeInfo
{
	// Only relevant for function types
	std::vector<VarTypeInfo> paramTypes;
	VarTypeInfo returnType;
};

bool BansheeCodeGeneratorASTVisitor::parseEventSignature(QualType type, FunctionTypeInfo& typeInfo, bool& isCallback)
{
	if (type->isStructureOrClassType())
	{
		const TemplateSpecializationType* specType = type->getAs<TemplateSpecializationType>();
		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = type->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName().str();
			std::string nsName = getNamespace(recordDecl);

			bool isEvent = false;
			if (sourceTypeName == "Event" && nsName == sFrameworkCppNs)
				isEvent = true;
			else if(sourceTypeName == "function" && recordDecl->isInStdNamespace())
			{
				isEvent = true;
				isCallback = true;
			}

			if (isEvent)
			{
				type = specType->getArg(0).getAsType();
				if(type->isFunctionProtoType())
				{
					const FunctionProtoType* funcType = type->getAs<FunctionProtoType>();

					unsigned int numParams = funcType->getNumParams();
					typeInfo.paramTypes.resize(numParams);

					for(unsigned int i = 0; i < numParams; i++)
					{
						QualType paramType = funcType->getParamType(i);
						parseType(paramType, typeInfo.paramTypes[i], false);
					}

					QualType returnType = funcType->getReturnType();
					if (!returnType->isVoidType())
						parseType(returnType, typeInfo.returnType, true);
					else
						typeInfo.returnType.flags = 0;
				}

				return true;
			}
		}
	}

	return false;
}

struct ParsedDeclInfo
{
	std::string exportName;
	std::string exportFile;
	std::string externalClass;
	CSVisibility visibility;
	int exportFlags;
	std::string moduleName;

	Style style;
};

void parseAttributeToken(const std::string& name, const std::string& value, StringRef sourceName, ParsedDeclInfo& output)
{
	if (name == "n" || name == "name")
		output.exportName = value;
	else if (name == "v" || name == "visibility")
	{
		if (value == "public")
			output.visibility = CSVisibility::Public;
		else if (value == "internal")
			output.visibility = CSVisibility::Internal;
		else if (value == "private")
			output.visibility = CSVisibility::Private;
		else
			outs() << "Warning: Unrecognized value for \"v\" option: \"" + value + "\" for type \"" <<
			sourceName << "\".\n";
	}
	else if (name == "f" || name == "file")
	{
		output.exportFile = value;
	}
	else if (name == "pl" || name == "plain")
	{
		output.exportFlags |= (int)ExportFlags::Plain;
	}
	else if (name == "pr" || name == "property")
	{
		if (value == "getter")
			output.exportFlags |= (int)ExportFlags::PropertyGetter;
		else if (value == "setter")
			output.exportFlags |= (int)ExportFlags::PropertySetter;
		else
		{
			outs() << "Warning: Unrecognized value for \"pr\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "api")
	{
		if (value == "bsf")
			output.exportFlags |= (int)ExportFlags::ApiBSF;
		else if (value == "b3d")
			output.exportFlags |= (int)ExportFlags::ApiB3D;
		else if (value == "bed")
			output.exportFlags |= (int)ExportFlags::ApiBED;
		else
		{
			outs() << "Warning: Unrecognized value for \"pr\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "e")
	{
		output.exportFlags |= (int)ExportFlags::External;

		output.externalClass = value;
	}
	else if (name == "ec")
	{
		output.exportFlags |= (int)ExportFlags::ExternalConstructor;

		output.externalClass = value;
	}
	else if (name == "ex")
	{
		if (value == "true")
			output.exportFlags |= (int)ExportFlags::Exclude;
		else if (value != "false")
		{
			outs() << "Warning: Unrecognized value for \"ex\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "in")
	{
		if (value == "true")
			output.exportFlags |= (int)ExportFlags::InteropOnly;
		else if (value != "false")
		{
			outs() << "Warning: Unrecognized value for \"in\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "m")
		output.moduleName = value;
	else if (name == "hide")
	{
		output.style.flags |= (int)StyleFlags::ForceHide;
	}
	else if (name == "show")
	{
		output.style.flags |= (int)StyleFlags::ForceShow;
	}
	else if (name == "layerMask")
	{
		output.style.flags |= (int)StyleFlags::AsLayerMask;
	}
	else if (name == "slider")
	{
		output.style.flags |= (int)StyleFlags::AsSlider;
	}
	else if (name == "notNull")
	{
		output.style.flags |= (int)StyleFlags::NotNull;
	}
	else if (name == "passByCopy")
	{
		output.style.flags |= (int)StyleFlags::PassByCopy;
	}
	else if (name == "applyOnDirty")
	{
		output.style.flags |= (int)StyleFlags::ApplyOnDirty;
	}
	else if (name == "asQuaternion")
	{
		output.style.flags |= (int)StyleFlags::AsQuaternion;
	}
	else if (name == "loadOnAssign")
	{
		output.style.flags |= (int)StyleFlags::LoadOnAssign;
	}
	else if (name == "hdr")
	{
		output.style.flags |= (int)StyleFlags::HDR;
	}
	else if (name == "step")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"step\" option for type \"" << sourceName << "\".\n";
		else
		{
			output.style.flags |= (int)StyleFlags::Step;
			output.style.step = atof(value.c_str());
		}
	}
	else if (name == "range")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"range\" option for type \"" << sourceName << "\".\n";
		else
		{
			std::vector<float> args;

			std::istringstream toParse(value);
			std::string arg;
			while(std::getline(toParse, arg, ','))
				args.push_back(atof(arg.c_str()));

			if(args.size() != 2)
				outs() << "Warning: Invalid number of arguments for \"range\" option for type \"" << sourceName << "\".\n";
			else
			{
				output.style.flags |= (int)StyleFlags::Range;
				output.style.rangeMin = args[0];
				output.style.rangeMax = args[1];
			}
		}
	}
	else if (name == "order")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"order\" option for type \"" << sourceName << "\".\n";
		else
		{
			output.style.flags |= (int)StyleFlags::Order;
			output.style.order = atoi(value.c_str());
		}
	}
	else if (name == "category")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"category\" option for type \"" << sourceName << "\".\n";
		else
		{
			std::vector<std::string> args;

			std::istringstream toParse(value);
			std::string arg;
			while(std::getline(toParse, arg, ','))
				args.push_back(arg);

			if(args.size() != 1)
				outs() << "Warning: Invalid number of arguments for \"category\" option for type \"" << sourceName << "\".\n";
			else
			{
				StringRef trimmedName = args[0];
				trimmedName = trimmedName.trim();

				output.style.flags |= (int)StyleFlags::Category;
				output.style.category = trimmedName.str();
			}
		}
	}
	else if (name == "inline")
	{
		output.style.flags |= (int)StyleFlags::Inline;
	}
	else
		outs() << "Warning: Unrecognized annotation attribute option: \"" + name + "\" for type \"" <<
		sourceName << "\".\n";
}

bool isExportAttribute(AnnotateAttr* attr)
{
	StringRef annotation = attr->getAnnotation();

	return annotation.startswith("se,");
}

bool parseExportAttribute(AnnotateAttr* attr, StringRef sourceName, ParsedDeclInfo& output)
{
	if(!isExportAttribute(attr))
		return false;

	StringRef annotation = attr->getAnnotation();

	output.exportName = sourceName.str();
	
	if (!output.exportName.empty())
	{
		// Camel case to pascal case
		if(islower(output.exportName[0]))
			output.exportName[0] = toupper(output.exportName[0]);
		else
		{
			// Screaming snake case to pascal case
			bool isScreamingSnakeCase = true;
			std::stringstream caseOutput;
			bool nextUpper = true;
			for(size_t i = 0; i < output.exportName.size(); i++)
			{
				if (isalpha(output.exportName[i]))
				{
					if(islower(output.exportName[i]))
					{
						isScreamingSnakeCase = false;
						break;
					}
					else
					{
						if(!nextUpper)
							caseOutput << (char)tolower(output.exportName[i]);
						else
						{
							caseOutput << output.exportName[i];
							nextUpper = false;
						}
					}
				}
				else if(output.exportName[i] == '_')
					nextUpper = true;
				else
					caseOutput << output.exportName[i];
			}

			if(isScreamingSnakeCase)
				output.exportName = caseOutput.str();
		}
	}

	output.exportFile = sourceName.str();
	output.visibility = CSVisibility::Public;
	output.exportFlags = 0;

	std::stringstream ssParamName;
	std::stringstream ssParamValue;

	bool isInScope = false;
	bool gotParamName = false;

	for (auto iter = annotation.begin() + 3; iter != annotation.end(); ++iter)
	{
		if(*iter == ' ' || *iter == '\t')
			continue;

		if(*iter == '[')
		{
			if(isInScope)
				outs() << "Error: Attribute parameter parsing error. Nested scopes not allowed.";
			else if(!gotParamName)
				outs() << "Error: Attribute parameter parsing error. Scopes not allowed for parameter names.";
			else
				isInScope = true;

			continue;
		}

		if(*iter == ']')
		{
			isInScope = false;
			continue;
		}

		if(*iter == ',')
		{
			if(isInScope)
				ssParamValue << ",";
			else
			{
				parseAttributeToken(ssParamName.str(), ssParamValue.str(), sourceName, output);
				
				ssParamName.str("");
				ssParamName.clear();

				ssParamValue.str("");
				ssParamValue.clear();

				gotParamName = false;
			}

			continue;
		}

		if(*iter == ':')
		{
			if(gotParamName)
				outs() << "Error: Attribute parameter parsing error. Found value separator while parsing value.";
			else
				gotParamName = true;

			continue;
		}

		if(!gotParamName)
			ssParamName << *iter;
		else
			ssParamValue << *iter;
	}

	if(!ssParamName.str().empty())
		parseAttributeToken(ssParamName.str(), ssParamValue.str(), sourceName, output);

	return true;
}

bool parseExportAttribute(Decl* decl, StringRef sourceName, ParsedDeclInfo& output)
{
	for (const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (parseExportAttribute(entry, sourceName, output))
			return true;
	}

	return false;
}

bool parseParamOrFieldAttribute(Decl* decl, bool isField, int& typeFlags)
{
	for(const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (!isField && entry->getAnnotation() == "params")
		{
			typeFlags |= (int)TypeFlags::VarParams;
			return true;
		}

		if (entry->getAnnotation() == "norref")
		{
			typeFlags &= ~(int)TypeFlags::AsResourceRef;
			return true;
		}
	}

	return false;
}

bool isBase(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	if (className == BUILTIN_COMPONENT_TYPE)
		return true;
	else if (className == BUILTIN_RESOURCE_TYPE)
		return true;
	else if (className == BUILTIN_SCENEOBJECT_TYPE)
		return true;
	else if (className == BUILTIN_MODULE_TYPE)
		return true;
	else if (className == BUILTIN_GUIELEMENT_TYPE)
		return true;
	else if (className == BUILTIN_REFLECTABLE_TYPE)
		return true;

	return false;
}

bool isExportable(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr != nullptr)
	{
		StringRef sourceClassName = decl->getName();
		ParsedDeclInfo parsedDeclInfo;

		if (parseExportAttribute(attr, sourceClassName, parsedDeclInfo))
			return true;
	}

	return false;
}

std::string parseExportableBaseClass(const CXXRecordDecl* decl)
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

			if(isBase(baseDecl))
			{
				iter++;
				continue;
			}

			AnnotateAttr* attr = baseDecl->getAttr<AnnotateAttr>();
			if (attr != nullptr)
			{
				StringRef sourceClassName = baseDecl->getName();
				ParsedDeclInfo parsedDeclInfo;

				if (parseExportAttribute(attr, sourceClassName, parsedDeclInfo))
					return sourceClassName.str();
			}

			todo.push(baseDecl);
			iter++;
		}
	}

	return "";
}

std::string parseExportableBaseStruct(const CXXRecordDecl* decl)
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
			ParsedDeclInfo parsedDeclInfo;

			if (parseExportAttribute(attr, sourceClassName, parsedDeclInfo))
			{
				if((parsedDeclInfo.exportFlags & (int)ExportFlags::Plain) != 0)
					return sourceClassName.str();
			}
		}

		iter++;
	}

	return "";
}

bool isModule(const CXXRecordDecl* decl)
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
			if (className == BUILTIN_MODULE_TYPE)
				return true;

			todo.push(baseDecl);
			iter++;
		}
	}

	return false;
}

BansheeCodeGeneratorASTVisitor::BansheeCodeGeneratorASTVisitor(CompilerInstance* CI, CommentParser& commentParser)
	:astContext(&(CI->getASTContext())), preprocessor(CI->getPreprocessor()), mCommentParser(commentParser)
{ }

bool BansheeCodeGeneratorASTVisitor::evaluateLiteral(Expr* expr, std::string& evalValue)
{
	QualType type = expr->getType();
	if (type->isBuiltinType())
	{
		const BuiltinType* builtinType = type->getAs<BuiltinType>();
		switch (builtinType->getKind())
		{
		case BuiltinType::Bool:
		{
			bool result;
			expr->EvaluateAsBooleanCondition(result, *astContext);

			evalValue = result ? "true" : "false";

			return true;
		}
		case BuiltinType::Char_S:
		case BuiltinType::Char_U:
		case BuiltinType::SChar:
		case BuiltinType::Short:
		case BuiltinType::Int:
		case BuiltinType::Long:
		case BuiltinType::LongLong:
		case BuiltinType::UChar:
		case BuiltinType::UShort:
		case BuiltinType::UInt:
		case BuiltinType::ULong:
		case BuiltinType::ULongLong:
		case BuiltinType::WChar_S:
		case BuiltinType::WChar_U:
		case BuiltinType::Char16:
		case BuiltinType::Char32:
		{
			Expr::EvalResult result;
			expr->EvaluateAsInt(result, *astContext);

			SmallString<5> valueStr;

			result.Val.getInt().toString(valueStr);
			evalValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::Float:
		{
			APFloat result(0.0f);
			expr->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evalValue = valueStr.str().str() + "f";

			return true;
		}
		case BuiltinType::Double:
		{
			APFloat result(0.0f);
			expr->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evalValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::NullPtr:
		{
			evalValue = "null";
			return true;
		}
		default:
			return false;
		}
	}
	else if(type->isEnumeralType())
	{
		const EnumType* enumType = type->getAs<EnumType>();
		const EnumDecl* enumDecl = enumType->getDecl();

		Expr::EvalResult result;
		expr->EvaluateAsInt(result, *astContext);

		SmallString<5> valueStr;
		result.Val.getInt().toString(valueStr);
		evalValue = valueStr.str().str();

		return true;
	}

	return false;
}

bool BansheeCodeGeneratorASTVisitor::evaluateExpression(Expr* expr, std::string& evalValue, std::string& valType)
{
	if (expr->isEvaluatable(*astContext))
	{
		if (evaluateLiteral(expr, evalValue))
			return true;
	}

	// Check for nullptr, literals in constructors and cast literals
	if (ExprWithCleanups* cleanups = dyn_cast<ExprWithCleanups>(expr))
		expr = cleanups->getSubExpr();

	// Skip through reference binding to temporary.
	if (MaterializeTemporaryExpr* materialize = dyn_cast<MaterializeTemporaryExpr>(expr))
		expr = materialize->getSubExpr();

	// Skip any temporary bindings; they're implicit.
	if (CXXBindTemporaryExpr* binder = dyn_cast<CXXBindTemporaryExpr>(expr))
		expr = binder->getSubExpr();

	expr = expr->IgnoreParenCasts();

	// Reference to some other declaration (e.g. a static)
	DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(expr);
	if(declRefExpr)
	{
		ValueDecl* decl = declRefExpr->getDecl();
		const std::string name = ParserUtility::GetFullName(decl);

		if(name == (sFrameworkCppNs + "::StringUtil::BLANK") || name == (sFrameworkCppNs + "::StringUtil::WBLANK"))
		{
			evalValue = "\"\"";
			valType = "";
			return true;
		}
		else if(name == (sFrameworkCppNs + "::UUID::EMPTY"))
		{
			evalValue = "";
			valType = "UUID";
			return true;
		}
	}

	CXXConstructExpr* ctorExp = dyn_cast<CXXConstructExpr>(expr);
	if (!ctorExp)
		return false;

	// Check for special case of a single null parameter
	{
		expr = ctorExp->getArg(0);

		bool isNull = false;
		QualType type = expr->getType();
		if (type->isBuiltinType())
		{
			const BuiltinType* builtinType = type->getAs<BuiltinType>();
			if (builtinType->getKind() == BuiltinType::NullPtr)
			{
				evalValue = "null";
				return true;
			}
		}
	}

	// Constructor or cast of some type
	QualType parentType = ctorExp->getType();

	VarTypeInfo varTypeInfo;
	parseType(parentType, varTypeInfo);
	valType = varTypeInfo.typeName;

	for(int i = 0; i < ctorExp->getNumArgs(); i++)
	{
		if (i != 0)
			evalValue += ", ";

		std::string argValue;
		expr = ctorExp->getArg(i);

		bool isNull = false;
		QualType type = expr->getType();
		if(type->isBuiltinType())
		{
			const BuiltinType* builtinType = type->getAs<BuiltinType>();
			if (builtinType->getKind() == BuiltinType::NullPtr)
			{
				argValue = "null";
				isNull = true;
			}
		}

		if(!isNull)
		{
			if (expr->isEvaluatable(*astContext))
			{
				if (!evaluateLiteral(expr, argValue))
					return false;
			}
			else
			{
				std::string dummy3;
				if (!evaluateExpression(expr, argValue, dummy3))
					return false;
			}
		}
			
		evalValue += argValue;
	}

	return true;
}

bool BansheeCodeGeneratorASTVisitor::parseEvent(ValueDecl* decl, const std::string& className, MethodInfo& eventInfo)
{
	AnnotateAttr* fieldAttr = decl->getAttr<AnnotateAttr>();
	if (fieldAttr == nullptr)
		return false;

	StringRef sourceFieldName = decl->getName();

	ParsedDeclInfo parsedEventInfo;
	if (!parseExportAttribute(fieldAttr, sourceFieldName, parsedEventInfo))
		return false;

	FunctionTypeInfo eventSignature;
	bool isCallback = false;
	if (!parseEventSignature(decl->getType(), eventSignature, isCallback))
		return false;

	if (decl->getAccess() != AS_public)
		outs() << "Error: Exported event \"" + sourceFieldName + "\" isn't public. This will likely result in invalid code generation.";

	int eventFlags = 0;

	if ((parsedEventInfo.exportFlags & (int)ExportFlags::External) != 0)
	{
		outs() << "Error: External events currently not supported. Skipping export for event \"" + sourceFieldName + "\".";
		return false;
	}

	if ((parsedEventInfo.exportFlags & (int)ExportFlags::InteropOnly))
		eventFlags |= (int)MethodFlags::InteropOnly;

	if (isCallback)
		eventFlags |= (int)MethodFlags::Callback;

	eventInfo.sourceName = sourceFieldName.str();
	eventInfo.scriptName = parsedEventInfo.exportName;
	eventInfo.flags = eventFlags;
	eventInfo.externalClass = className;
	eventInfo.visibility = parsedEventInfo.visibility;
	eventInfo.api = apiFromExportFlags(parsedEventInfo.exportFlags);
	mCommentParser.ParseComments(decl, eventInfo.documentation);
	clearParamRefComments(eventInfo.documentation);

	if (!eventSignature.returnType.typeName.empty())
	{
		eventInfo.returnInfo.typeName = eventSignature.returnType.typeName;
		eventInfo.returnInfo.flags = eventSignature.returnType.flags;
	}

	int idx = 0;
	for(auto& entry : eventSignature.paramTypes)
	{
		VarInfo paramInfo;
		paramInfo.flags = entry.flags;
		paramInfo.typeName = entry.typeName;
		paramInfo.name = "p" + std::to_string(idx);

		eventInfo.paramInfos.push_back(paramInfo);
		idx++;
	}

	return true;
}

void parseNamespace(NamedDecl* decl, SmallVector<std::string, 4>& output)
{
	const DeclContext* context = decl->getDeclContext();
	SmallVector<const DeclContext *, 8> contexts;

	// Collect contexts.
	while (context && isa<NamedDecl>(context))
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	for (const DeclContext* dc : reverse(contexts))
	{
		if (const auto* nd = dyn_cast<NamespaceDecl>(dc))
		{
			if (!nd->isAnonymousNamespace())
				output.push_back(nd->getDeclName().getAsString());
		}
	}
}

bool BansheeCodeGeneratorASTVisitor::VisitEnumDecl(EnumDecl* decl)
{
	mCommentParser.ParseAndRegisterAllComments(decl);

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef sourceClassName = decl->getName();
	ParsedDeclInfo parsedEnumInfo;
	parsedEnumInfo.exportName = sourceClassName.str();

	if (!parseExportAttribute(attr, sourceClassName, parsedEnumInfo))
		return true;

	FileInfo& fileInfo = outputFileInfos[parsedEnumInfo.exportFile];
	auto iterFind = std::find_if(fileInfo.enumInfos.begin(), fileInfo.enumInfos.end(), 
		[&sourceClassName](const EnumInfo& ei)
	{
		return ei.name == sourceClassName;
	});

	if (iterFind != fileInfo.enumInfos.end())
		return true; // Already parsed

	QualType underlyingType = decl->getIntegerType();
	if (!underlyingType->isBuiltinType())
	{
		outs() << "Error: Found an enum with non-builtin underlying type, skipping.\n";
		return true;
	}

	EnumInfo enumEntry;
	enumEntry.name = sourceClassName.str();
	enumEntry.scriptName = parsedEnumInfo.exportName;
	enumEntry.visibility = parsedEnumInfo.visibility;
	enumEntry.api = apiFromExportFlags(parsedEnumInfo.exportFlags);
	enumEntry.module = parsedEnumInfo.moduleName;
	mCommentParser.ParseComments(decl, enumEntry.documentation);
	clearParamRefComments(enumEntry.documentation);

	parseNamespace(decl, enumEntry.ns);

	const BuiltinType* builtinType = underlyingType->getAs<BuiltinType>();

	std::string enumType;
	if (builtinType->getKind() != BuiltinType::Kind::Int)
		mapBuiltinTypeToCSType(builtinType->getKind(), enumEntry.explicitType);

	std::string declFile = astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
	std::string destFile = "BsScript" + parsedEnumInfo.exportFile + ".generated.h";
	std::string destFileEditor = "BsScript" + parsedEnumInfo.exportFile + ".editor.generated.h";

	registerUserTypeInfo(enumEntry.ns, sourceClassName.str(), enumEntry.api, declFile, parsedEnumInfo.exportName,
		parsedEnumInfo.exportFile, ::ParsedType::Enum);
	cppToCsTypeMap[sourceClassName.str()].underlyingType = builtinType->getKind();

	auto iter = decl->enumerator_begin();
	while (iter != decl->enumerator_end())
	{
		EnumConstantDecl* constDecl = *iter;

		ParsedDeclInfo parsedEnumEntryInfo;
		AnnotateAttr* enumAttr = constDecl->getAttr<AnnotateAttr>();

		StringRef entryName = constDecl->getName();
		parsedEnumEntryInfo.exportName = entryName.str();
		parsedEnumEntryInfo.exportFlags = 0;

		if (enumAttr != nullptr)
			parseExportAttribute(enumAttr, entryName, parsedEnumEntryInfo);

		if ((parsedEnumEntryInfo.exportFlags & (int)ExportFlags::Exclude) != 0)
		{
			++iter;
			continue;
		}

		const APSInt& entryVal = constDecl->getInitVal();

		EnumEntryInfo entryInfo;
		entryInfo.name = entryName.str();
		entryInfo.scriptName = parsedEnumEntryInfo.exportName;
		mCommentParser.ParseComments(constDecl, entryInfo.documentation);
		clearParamRefComments(entryInfo.documentation);

		SmallString<5> valueStr;
		entryVal.toString(valueStr);
		entryInfo.value = valueStr.str().str();

		enumEntry.entries[(int)entryVal.getExtValue()] = entryInfo;
		++iter;
	}

	addEntryToFile<EnumInfo>(fileInfo, enumEntry, parsedEnumInfo.exportFile, 
		[](FileInfo& fileInfo, const EnumInfo& enumInfo) { fileInfo.enumInfos.push_back(enumInfo); });

	return true;
}


std::string BansheeCodeGeneratorASTVisitor::parseTemplArguments(const std::string& className, const TemplateArgument* tmplArgs, unsigned numArgs, SmallVector<TemplateParamInfo, 0>* templParams)
{
	std::stringstream tmplArgsStream;
	tmplArgsStream << "<";
	for(unsigned i = 0; i < numArgs; i++)
	{
		if (i != 0)
			tmplArgsStream << ", ";

		auto& tmplArg = tmplArgs[i];
		if(tmplArg.getKind() == TemplateArgument::Type)
		{
			VarTypeInfo varTypeInfo;
			parseType(tmplArg.getAsType(), varTypeInfo, false);

			tmplArgsStream << varTypeInfo.typeName;

			if(templParams != nullptr)
				templParams->push_back({ "class" });
		}
		else if(tmplArg.getKind() == TemplateArgument::Expression)
		{
			std::string tmplArgExprValue, exprType;
			if (!evaluateExpression(tmplArg.getAsExpr(), tmplArgExprValue, exprType))
			{
				outs() << "Error: Template argument for type \"" << className << "\" cannot be constantly evaluated, ignoring it.\n";
				tmplArgsStream << "unknown";
			}
			else
				tmplArgsStream << tmplArgExprValue;

			VarTypeInfo varTypeInfo;
			parseType(tmplArg.getAsExpr()->getType(), varTypeInfo, false);

			if(templParams != nullptr)
				templParams->push_back({ varTypeInfo.typeName });
		}
		else
		{
			outs() << "Error: Cannot parse template argument for type: \"" << className << "\". \n";
			tmplArgsStream << "unknown";

			if(templParams != nullptr)
				templParams->push_back({ "unknown" });
		}
	}

	tmplArgsStream << ">";
	return tmplArgsStream.str();
}

bool BansheeCodeGeneratorASTVisitor::VisitCXXRecordDecl(CXXRecordDecl* decl)
{
	mCommentParser.ParseAndRegisterAllComments(decl);

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef declName = decl->getName();

	ParsedDeclInfo parsedClassInfo;
	parsedClassInfo.exportName = declName.str();

	if (!parseExportAttribute(attr, declName, parsedClassInfo))
		return true;

	std::string srcClassName = declName.str();

	// If a template specialization append template params to its name
	ClassTemplateSpecializationDecl* specDecl = dyn_cast<ClassTemplateSpecializationDecl>(decl);
	CXXRecordDecl* templatedDecl = decl;
	SmallVector<TemplateParamInfo, 0> templParams;
	if(specDecl != nullptr)
	{
		auto& tmplArgs = specDecl->getTemplateInstantiationArgs();
		srcClassName += parseTemplArguments(srcClassName, tmplArgs.data(), tmplArgs.size(), &templParams);
		templatedDecl = specDecl->getSpecializedTemplate()->getTemplatedDecl();
	}

	FileInfo& fileInfo = outputFileInfos[parsedClassInfo.exportFile];
	if ((parsedClassInfo.exportFlags & (int)ExportFlags::Plain) != 0)
	{
		auto iterFind = std::find_if(fileInfo.structInfos.begin(), fileInfo.structInfos.end(), 
			[&srcClassName](const StructInfo& si)
		{
			return si.name == srcClassName;
		});

		if (iterFind != fileInfo.structInfos.end())
			return true; // Already parsed

		StructInfo structInfo;
		structInfo.name = srcClassName;
		structInfo.cleanName = declName.str();
		structInfo.baseClass = parseExportableBaseStruct(decl);
		structInfo.visibility = parsedClassInfo.visibility;
		structInfo.requiresInterop = decl->isPolymorphic();
		structInfo.module = parsedClassInfo.moduleName;
		structInfo.isTemplateInst = specDecl != nullptr;
		structInfo.templParams = templParams;
		structInfo.api = apiFromExportFlags(parsedClassInfo.exportFlags);

		mCommentParser.ParseComments(templatedDecl, structInfo.documentation);
		parseNamespace(decl, structInfo.ns);
		clearParamRefComments(structInfo.documentation);

		std::unordered_map<FieldDecl*, std::pair<std::string, std::string>> defaultFieldValues;

		// Parse non-default constructors & determine default values for fields
		if (decl->hasUserDeclaredConstructor())
		{
			auto ctorIter = decl->ctor_begin();
			while (ctorIter != decl->ctor_end())
			{
				SimpleConstructorInfo ctorInfo;
				CXXConstructorDecl* ctorDecl = *ctorIter;

				if (ctorDecl->isImplicit())
				{
					++ctorIter;
					continue;
				}

				AnnotateAttr* ctorAttr = ctorDecl->getAttr<AnnotateAttr>();
				if (ctorAttr != nullptr)
				{
					ParsedDeclInfo parsedCtorInfo;
					parseExportAttribute(ctorAttr, srcClassName, parsedCtorInfo);

					if ((parsedCtorInfo.exportFlags & (int)ExportFlags::Exclude) != 0)
					{
						++ctorIter;
						continue;
					}
				}

				mCommentParser.ParseComments(ctorDecl, ctorInfo.documentation);

				bool skippedDefaultArgument = false;
				for (auto I = ctorDecl->param_begin(); I != ctorDecl->param_end(); ++I)
				{
					ParmVarDecl* paramDecl = *I;

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName().str();

					std::string typeName;
					unsigned arraySize;
					if (!parseType(paramDecl->getType(), paramInfo))
					{
						outs() << "Error: Unable to detect type for constructor parameter \"" << paramDecl->getName().str()
							<< "\". Skipping.\n";
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArgument)
					{
						if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue, paramInfo.defaultValueType))
						{
							outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
								<< "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArgument = true;
						}
					}

					ctorInfo.params.push_back(paramInfo);
				}

				std::unordered_map<FieldDecl*, ParmVarDecl*> assignments;

				// Parse initializers for assignments & default values
				for (auto I = ctorDecl->init_begin(); I != ctorDecl->init_end(); ++I)
				{
					CXXCtorInitializer* init = *I;

					if (init->isMemberInitializer())
					{
						FieldDecl* field = init->getMember();
						Expr* initExpr = init->getInit();

						bool isValid = true;
						while(CXXConstructExpr* constructExpr = dyn_cast<CXXConstructExpr>(initExpr))
						{
							isValid = false;
							if(constructExpr->getNumArgs() == 0)
							{
								// Don't care about default constructors
								break;
							}
							else if (constructExpr->getNumArgs() == 1)
							{
								initExpr = constructExpr->getArg(0);
								isValid = true;
							}
							else
							{
								outs() << "Error: Invalid number of parameters in constructor initializer. Only one parameter "
									"constructors are supported. In struct \"" + srcClassName + "\".\n";
								break;
							}
						}

						// Let the member initializer code handle the default value
						if (dyn_cast<CXXDefaultInitExpr>(initExpr))
							isValid = false;
						
						if (isValid)
						{
							// Check for constant value first
							std::string evalValue, evalTypeValue;
							if (evaluateExpression(initExpr, evalValue, evalTypeValue))
								defaultFieldValues[field] = std::make_pair(evalValue, evalTypeValue);
							else // Check for initializers referencing parameters
							{
								Decl* varDecl = nullptr;

								// Check for std::move
								if (CallExpr* callExpr = dyn_cast<CallExpr>(initExpr))
								{
									if(FunctionDecl* funcDecl = dyn_cast<FunctionDecl>(callExpr->getCalleeDecl()))
									{
										if(funcDecl->getName() == "move" && funcDecl->isInStdNamespace())
										{
											if(callExpr->getNumArgs() == 1)
											{
												if (Expr* argExpr = callExpr->getArg(0))
													varDecl = argExpr->getReferencedDeclOfCallee();
											}
										}
										
									}
								}
								else
								{
									varDecl = initExpr->getReferencedDeclOfCallee();
								}

								if (varDecl != nullptr)
								{
									ParmVarDecl* parmVarDecl = dyn_cast<ParmVarDecl>(varDecl);
									if (parmVarDecl != nullptr)
										assignments[field] = parmVarDecl;
								}
								else
								{
									std::string fieldName;

									if (field)
										fieldName = field->getName().str();

									outs() << "Error: Unrecognized initializer format in struct \"" << srcClassName << "\" for field \"" << fieldName << "\".\n";
								}
							}
						}
					}
				}

				// Parse any assignments in the function body
				// Note: Searching for trivially simple assignments only, ignoring anything else
				if (ctorDecl->hasBody())
				{
					CompoundStmt* functionBody = dyn_cast<CompoundStmt>(ctorDecl->getBody()); // Note: Not handling inner blocks
					assert(functionBody != nullptr);

					for (auto I = functionBody->child_begin(); I != functionBody->child_end(); ++I)
					{
						Stmt* stmt = *I;

						BinaryOperator* binaryOp = dyn_cast<BinaryOperator>(stmt);
						if (binaryOp == nullptr)
							continue;

						if (binaryOp->getOpcode() != BO_Assign)
							continue;

						Expr* lhsExpr = binaryOp->getLHS()->IgnoreParenCasts(); // Note: Ignoring even explicit casts
						Decl* lhsDecl;

						if (DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(lhsExpr))
							lhsDecl = varExpr->getDecl();
						else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(lhsExpr))
							lhsDecl = memberExpr->getMemberDecl();
						else
							continue;

						FieldDecl* fieldDecl = dyn_cast<FieldDecl>(lhsDecl);
						if (fieldDecl == nullptr)
							continue;

						Expr* rhsExpr = binaryOp->getRHS()->IgnoreParenCasts();
						Decl* rhsDecl = nullptr;

						if (DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(rhsExpr))
							rhsDecl = varExpr->getDecl();
						else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(rhsExpr))
							rhsDecl = memberExpr->getMemberDecl();

						ParmVarDecl* parmVarDecl = nullptr;
						if (rhsDecl != nullptr)
							parmVarDecl = dyn_cast<ParmVarDecl>(rhsDecl);

						if (parmVarDecl == nullptr)
						{
							outs() << "Warning: Found a non-trivial field assignment for field \"" << fieldDecl->getName() << "\" in"
								<< " constructor of \"" << srcClassName << "\". Ignoring assignment.\n";
							continue;
						}

						assignments[fieldDecl] = parmVarDecl;
					}
				}

				for (auto I = decl->field_begin(); I != decl->field_end(); ++I)
				{
					auto iterFind = assignments.find(*I);
					if (iterFind == assignments.end())
						continue;

					std::string fieldName = iterFind->first->getName().str();
					std::string paramName = iterFind->second->getName().str();

					ctorInfo.fieldAssignments[fieldName] = paramName;
				}

				updateParamRefComments(ctorInfo.params, ctorInfo.documentation);

				structInfo.ctors.push_back(ctorInfo);
				++ctorIter;
			}
		}

		std::stack<const CXXRecordDecl*> todo;
		todo.push(decl);

		bool hasDefaultValue = false;
		while (!todo.empty())
		{
			const CXXRecordDecl* curDecl = todo.top();
			todo.pop();

			for (auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
			{
				FieldDecl* fieldDecl = *I;
				FieldInfo fieldInfo;
				fieldInfo.name = fieldDecl->getName().str();

				ParsedDeclInfo parsedFieldInfo;
				if (parseExportAttribute(fieldDecl, srcClassName, parsedFieldInfo))
				{
					if ((parsedFieldInfo.exportFlags & (int)ExportFlags::Exclude) != 0)
					{
						structInfo.requiresInterop = true;
						continue;
					}

					fieldInfo.style = parsedFieldInfo.style;
				}

				auto iterFind = defaultFieldValues.find(fieldDecl);
				if (iterFind != defaultFieldValues.end())
				{
					fieldInfo.defaultValue = iterFind->second.first;
					fieldInfo.defaultValueType = iterFind->second.second;
				}

				if (fieldDecl->hasInClassInitializer())
				{
					Expr* initExpr = fieldDecl->getInClassInitializer();

					evaluateExpression(initExpr, fieldInfo.defaultValue, fieldInfo.defaultValueType);
				}

				std::string typeName;
				if (!parseType(fieldDecl->getType(), fieldInfo))
				{
					outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
						<< srcClassName << "\". Skipping field.\n";
					continue;
				}

				parseParamOrFieldAttribute(fieldDecl, true, fieldInfo.flags);

				// Remove the pass-as-resource-ref flag to all parameters initializing the field
				if(!getPassAsResourceRef(fieldInfo.flags))
				{
					for(auto& ctorInfo : structInfo.ctors)
					{
						auto iterFindField = ctorInfo.fieldAssignments.find(fieldInfo.name);
						if (iterFindField != ctorInfo.fieldAssignments.end())
						{
							auto iterFindParam = std::find_if(ctorInfo.params.begin(), ctorInfo.params.end(), 
								[name = iterFindField->second](const VarInfo& varInfo)
							{
								return varInfo.name == name;
							});

							if (iterFindParam != ctorInfo.params.end())
								iterFindParam->flags &= ~(int)TypeFlags::AsResourceRef;
						}
					}
				}

				if (!fieldInfo.defaultValue.empty())
					hasDefaultValue = true;

				mCommentParser.ParseComments(fieldDecl, fieldInfo.documentation);
				clearParamRefComments(fieldInfo.documentation);

				structInfo.fields.push_back(fieldInfo);
			}

			auto iter = curDecl->bases_begin();
			while (iter != curDecl->bases_end())
			{
				const CXXBaseSpecifier* baseSpec = iter;
				CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

				todo.push(baseDecl);
				iter++;
			}
		}

		// If struct has in-class default values assigned, but no explicit constructors, add a parameterless constructor
		if (structInfo.ctors.empty() && hasDefaultValue)
			structInfo.ctors.push_back(SimpleConstructorInfo());

		std::string declFile = astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
		registerUserTypeInfo(structInfo.ns, srcClassName, structInfo.api, declFile, parsedClassInfo.exportName,
			parsedClassInfo.exportFile, ::ParsedType::Struct);

		addEntryToFile<StructInfo>(fileInfo, structInfo, parsedClassInfo.exportFile,
			[](FileInfo& fileInfo, const StructInfo& structInfo) { fileInfo.structInfos.push_back(structInfo); });
	}
	else
	{
		auto iterFind = std::find_if(fileInfo.classInfos.begin(), fileInfo.classInfos.end(), 
			[&srcClassName](const ClassInfo& ci)
		{
			return ci.name == srcClassName;
		});

		if (iterFind != fileInfo.classInfos.end())
			return true; // Already parsed

		ClassInfo classInfo;
		classInfo.name = srcClassName;
		classInfo.cleanName = declName.str();
		classInfo.visibility = parsedClassInfo.visibility;
		classInfo.api = apiFromExportFlags(parsedClassInfo.exportFlags);
		classInfo.flags = 0;
		classInfo.baseClass = parseExportableBaseClass(decl);
		classInfo.module = parsedClassInfo.moduleName;
		classInfo.templParams = templParams;
		mCommentParser.ParseComments(templatedDecl, classInfo.documentation);
		clearParamRefComments(classInfo.documentation);

		parseNamespace(decl, classInfo.ns);

		if ((parsedClassInfo.style.flags & (int)StyleFlags::ForceHide) != 0)
			classInfo.flags |= (int)ClassFlags::HideInInspector;

		if (specDecl != nullptr)
			classInfo.flags |= (int)ClassFlags::IsTemplateInst;

		bool clsIsModule = isModule(decl);
		if (clsIsModule)
			classInfo.flags |= (int)ClassFlags::IsModule;

		if (decl->isStruct())
			classInfo.flags |= (int)ClassFlags::IsStruct;

		::ParsedType classType = getObjectType(decl);

		std::string declFile = astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
		registerUserTypeInfo(classInfo.ns, srcClassName, classInfo.api, declFile, parsedClassInfo.exportName,
			parsedClassInfo.exportFile, classType);

		std::stack<const CXXRecordDecl*> todo;
		todo.push(decl);

		while (!todo.empty())
		{
			const CXXRecordDecl* curDecl = todo.top();
			todo.pop();

			// Parse constructors for non-module (singleton) classes
			if (!clsIsModule)
			{
				for (auto I = curDecl->ctor_begin(); I != curDecl->ctor_end(); ++I)
				{
					CXXConstructorDecl* ctorDecl = *I;

					AnnotateAttr* methodAttr = ctorDecl->getAttr<AnnotateAttr>();
					if (methodAttr == nullptr)
						continue;

					StringRef dummy;
					ParsedDeclInfo parsedMethodInfo;
					if (!parseExportAttribute(methodAttr, dummy, parsedMethodInfo))
						continue;

					MethodInfo methodInfo;
					methodInfo.sourceName = declName.str();
					methodInfo.scriptName = parsedClassInfo.exportName;
					methodInfo.flags = (int)MethodFlags::Constructor;
					methodInfo.visibility = parsedMethodInfo.visibility;
					methodInfo.api = apiFromExportFlags(parsedMethodInfo.exportFlags);
					mCommentParser.ParseComments(ctorDecl, methodInfo.documentation);

					if ((parsedMethodInfo.exportFlags & (int)ExportFlags::InteropOnly))
						methodInfo.flags |= (int)MethodFlags::InteropOnly;

					bool invalidParam = false;
					bool skippedDefaultArg = false;
					for (auto J = ctorDecl->param_begin(); J != ctorDecl->param_end(); ++J)
					{
						ParmVarDecl* paramDecl = *J;
						QualType paramType = paramDecl->getType();

						VarInfo paramInfo;
						paramInfo.name = paramDecl->getName().str();

						if (!parseType(paramType, paramInfo))
						{
							outs() << "Error: Unable to parse parameter \"" << paramInfo.name << "\" type in \"" << srcClassName << "\"'s constructor.\n";
							invalidParam = true;
							continue;
						}

						if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
						{
							if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue, paramInfo.defaultValueType))
							{
								outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
									<< "argument that cannot be constantly evaluated, ignoring it.\n";
								skippedDefaultArg = true;
							}
						}

						parseParamOrFieldAttribute(paramDecl, false, paramInfo.flags);
						methodInfo.paramInfos.push_back(paramInfo);
					}

					if (invalidParam)
						continue;

					updateParamRefComments(methodInfo.paramInfos, methodInfo.documentation);
					classInfo.ctorInfos.push_back(methodInfo);
				}
			}

			for (auto I = curDecl->method_begin(); I != curDecl->method_end(); ++I)
			{
				CXXMethodDecl* methodDecl = *I;

				CXXConstructorDecl* ctorDecl = dyn_cast<CXXConstructorDecl>(methodDecl);
				if (ctorDecl != nullptr)
					continue;

				if (!methodDecl->isUserProvided() || methodDecl->isImplicit())
					continue;

				AnnotateAttr* methodAttr = methodDecl->getAttr<AnnotateAttr>();
				if (methodAttr == nullptr)
					continue;

				StringRef sourceMethodName = methodDecl->getName();

				ParsedDeclInfo parsedMethodInfo;
				if (!parseExportAttribute(methodDecl, sourceMethodName, parsedMethodInfo))
					continue;

				if (methodDecl->getAccess() != AS_public)
					outs() << "Error: Exported method \"" + sourceMethodName + "\" isn't public. This will likely result in invalid code generation.";

				int methodFlags = 0;

				bool isExternal = false;
				if ((parsedMethodInfo.exportFlags & (int)ExportFlags::External) != 0)
				{
					methodFlags |= (int)MethodFlags::External;
					isExternal = true;
				}

				if ((parsedMethodInfo.exportFlags & (int)ExportFlags::ExternalConstructor) != 0)
				{
					methodFlags |= (int)MethodFlags::External;
					methodFlags |= (int)MethodFlags::Constructor;

					isExternal = true;
				}

				if ((parsedMethodInfo.exportFlags & (int)ExportFlags::InteropOnly))
					methodFlags |= (int)MethodFlags::InteropOnly;

				bool isStatic = false;
				if (methodDecl->isStatic() && !isExternal) // Note: Perhaps add a way to mark external methods as static
				{
					methodFlags |= (int)MethodFlags::Static;
					isStatic = true;
				}

				if ((parsedMethodInfo.exportFlags & (int)ExportFlags::PropertyGetter) != 0)
					methodFlags |= (int)MethodFlags::PropertyGetter;
				else if ((parsedMethodInfo.exportFlags & (int)ExportFlags::PropertySetter) != 0)
					methodFlags |= (int)MethodFlags::PropertySetter;

				MethodInfo methodInfo;
				methodInfo.sourceName = sourceMethodName.str();
				methodInfo.scriptName = parsedMethodInfo.exportName;
				methodInfo.flags = methodFlags;
				methodInfo.externalClass = srcClassName;
				methodInfo.visibility = parsedMethodInfo.visibility;
				methodInfo.api = apiFromExportFlags(parsedMethodInfo.exportFlags);
				methodInfo.style = parsedMethodInfo.style;
				mCommentParser.ParseComments(methodDecl, methodInfo.documentation);

				bool isProperty = (parsedMethodInfo.exportFlags & ((int)ExportFlags::PropertyGetter | (int)ExportFlags::PropertySetter));

				if (!isProperty)
				{
					QualType returnType = methodDecl->getReturnType();
					if (!returnType->isVoidType())
					{
						ReturnInfo returnInfo;
						if (!parseType(returnType, returnInfo, true))
						{
							outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
							continue;
						}

						parseParamOrFieldAttribute(methodDecl, false, returnInfo.flags);
						methodInfo.returnInfo = returnInfo;
					}
				}
				else
				{
					if ((parsedMethodInfo.exportFlags & (int)ExportFlags::PropertyGetter) != 0)
					{
						QualType returnType = methodDecl->getReturnType();
						if (returnType->isVoidType())
						{
							outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
								<< "\" has no return value.\n";
							continue;
						}

						// Note: I can potentially allow an output parameter instead of a return value
						if (methodDecl->param_size() > 1 || ((!isExternal || isStatic) && methodDecl->param_size() > 0))
						{
							outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
								<< "\" has parameters.\n";
							continue;
						}

						if (!parseType(returnType, methodInfo.returnInfo, true))
						{
							outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
							continue;
						}

						parseParamOrFieldAttribute(methodDecl, false, methodInfo.returnInfo.flags);
					}
					else // Must be setter
					{
						QualType returnType = methodDecl->getReturnType();
						if (!returnType->isVoidType())
						{
							outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
								<< "\" has a return value.\n";
							continue;
						}

						if (methodDecl->param_size() == 0 || methodDecl->param_size() > 2 || ((!isExternal || isStatic) && methodDecl->param_size() != 1))
						{
							outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
								<< "\" has more or less than one parameter.\n";
							continue;
						}

						ParmVarDecl* paramDecl = methodDecl->getParamDecl(0);

						VarInfo paramInfo;
						paramInfo.name = paramDecl->getName().str();

						if (!parseType(paramDecl->getType(), paramInfo))
						{
							outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
							continue;
						}
					}
				}

				bool invalidParam = false;
				bool skippedDefaultArg = false;
				for (auto J = methodDecl->param_begin(); J != methodDecl->param_end(); ++J)
				{
					ParmVarDecl* paramDecl = *J;
					QualType paramType = paramDecl->getType();

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName().str();

					if (!parseType(paramType, paramInfo))
					{
						outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
						invalidParam = true;
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
					{
						Expr* defaultArg;
						if (paramDecl->hasUninstantiatedDefaultArg())
							defaultArg = paramDecl->getUninstantiatedDefaultArg();
						else
							defaultArg = paramDecl->getDefaultArg();

						if (!evaluateExpression(defaultArg, paramInfo.defaultValue, paramInfo.defaultValueType))
						{
							outs() << "Error: Method parameter \"" << paramDecl->getName().str() << "\" has a default "
								<< "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArg = true;
						}
					}

					parseParamOrFieldAttribute(paramDecl, false, paramInfo.flags);
					methodInfo.paramInfos.push_back(paramInfo);
				}

				if (invalidParam)
					continue;

				updateParamRefComments(methodInfo.paramInfos, methodInfo.documentation);

				if (isExternal)
				{
					if (parsedMethodInfo.externalClass == "T")
						parsedMethodInfo.externalClass = srcClassName;

					ExternalClassInfos& infos = externalClassInfos[parsedMethodInfo.externalClass];
					infos.methods.push_back(methodInfo);
				}
				else
					classInfo.methodInfos.push_back(methodInfo);
			}

			// Look for exported fields & events
			for (auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
			{
				FieldDecl* fieldDecl = *I;

				MethodInfo eventInfo;
				if (parseEvent(fieldDecl, srcClassName, eventInfo))
					classInfo.eventInfos.push_back(eventInfo);
				else
				{
					FieldInfo fieldInfo;
					fieldInfo.name = fieldDecl->getName().str();

					ParsedDeclInfo parsedFieldInfo;
					bool foundExportAttrib = false;
					for(const auto& entry : fieldDecl->specific_attrs<AnnotateAttr>())
					{
						if(isExportAttribute(entry))
						{
							if (parseExportAttribute(entry, fieldInfo.name, parsedFieldInfo))
								foundExportAttrib = true;

							break;
						}
					}

					if(!foundExportAttrib)
						continue;

					std::string typeName;
					if (!parseType(fieldDecl->getType(), fieldInfo))
					{
						outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
							<< srcClassName << "\". Skipping field.\n";
						continue;
					}

					if (fieldDecl->getAccess() != AS_public)
						outs() << "Error: Exported field \"" + fieldInfo.name + "\" isn't public. This will likely result in invalid code generation.";

					fieldInfo.style = parsedFieldInfo.style;

					mCommentParser.ParseComments(fieldDecl, fieldInfo.documentation);
					clearParamRefComments(fieldInfo.documentation);

					classInfo.fieldInfos.push_back(fieldInfo);

					// Register wrapper methods, this way we can re-use much of the same logic for method/property generation
					MethodInfo getterInfo;
					getterInfo.sourceName = "Get" + fieldInfo.name;
					getterInfo.scriptName = parsedFieldInfo.exportName;
					getterInfo.visibility = parsedFieldInfo.visibility;
					getterInfo.api = apiFromExportFlags(parsedFieldInfo.exportFlags);
					getterInfo.flags = (int)MethodFlags::PropertyGetter | (int)MethodFlags::FieldWrapper;
					getterInfo.style = fieldInfo.style;

					getterInfo.returnInfo.flags = fieldInfo.flags;
					getterInfo.returnInfo.arraySize = fieldInfo.arraySize;
					getterInfo.returnInfo.typeName = fieldInfo.typeName;
					parseParamOrFieldAttribute(fieldDecl, true, getterInfo.returnInfo.flags);

					if ((parsedFieldInfo.exportFlags & (int)ExportFlags::InteropOnly) != 0)
						getterInfo.flags |= (int)MethodFlags::InteropOnly;

					VarInfo paramInfo;
					paramInfo.flags = fieldInfo.flags;
					paramInfo.arraySize = fieldInfo.arraySize;
					paramInfo.typeName = fieldInfo.typeName;
					paramInfo.name = "value";

					parseParamOrFieldAttribute(fieldDecl, true, paramInfo.flags);

					MethodInfo setterInfo;
					setterInfo.sourceName = "Set" + fieldInfo.name;
					setterInfo.scriptName = parsedFieldInfo.exportName;
					setterInfo.documentation = fieldInfo.documentation;
					setterInfo.paramInfos.push_back(paramInfo);
					setterInfo.visibility = parsedFieldInfo.visibility;
					setterInfo.api = apiFromExportFlags(parsedFieldInfo.exportFlags);
					setterInfo.flags = (int)MethodFlags::PropertySetter | (int)MethodFlags::FieldWrapper;
					setterInfo.style = fieldInfo.style;

					if ((parsedFieldInfo.exportFlags & (int)ExportFlags::InteropOnly) != 0)
						setterInfo.flags |= (int)MethodFlags::InteropOnly;

					classInfo.methodInfos.push_back(getterInfo);
					classInfo.methodInfos.push_back(setterInfo);
				}
			}

			// Find static data events
			const DeclContext* context = dyn_cast<DeclContext>(curDecl);
			for (auto I = context->decls_begin(); I != context->decls_end(); ++I)
			{
				if (VarDecl* varDecl = dyn_cast<VarDecl>(*I))
				{
					if (!varDecl->isStaticDataMember())
						continue;

					MethodInfo eventInfo;
					if (!parseEvent(varDecl, srcClassName, eventInfo))
						continue;

					eventInfo.flags |= (int)MethodFlags::Static;
					classInfo.eventInfos.push_back(eventInfo);
				}
			}

			auto iter = curDecl->bases_begin();
			while (iter != curDecl->bases_end())
			{
				const CXXBaseSpecifier* baseSpec = iter;
				CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

				// Base classes never need to be exported. Exportable classes will handle their own methods/fields.
				if (isBase(baseDecl) || isExportable(baseDecl))
				{
					iter++;
					continue;
				}

				todo.push(baseDecl);
				iter++;
			}
		}

		// External classes are just containers for external methods, we don't need to process them directly
		if ((parsedClassInfo.exportFlags & (int)ExportFlags::External) == 0)
		{
			addEntryToFile<ClassInfo>(fileInfo, classInfo, parsedClassInfo.exportFile,
				[](FileInfo& fileInfo, const ClassInfo& classInfo) { fileInfo.classInfos.push_back(classInfo); });
		}
	}

	return true;
}
