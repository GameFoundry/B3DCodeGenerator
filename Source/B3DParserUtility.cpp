#include "B3DParserUtility.h"
#include "B3DCommentParser.h"

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

void ParserUtility::PostProcessFileInfos(CommentParser& commentParser)
{
	// Inject external methods into their appropriate class infos
	auto findClassInfo = [](const std::string& name, bool isEditor) -> ClassInfo*
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				if (classInfo.name != name)
					continue;

				// Two versions of editor and BSF class migth exist, make sure to pick the right one
				if((isEditor && classInfo.api == ApiFlags::BSF) || (!isEditor &&  hasAPIBED(classInfo.api)))
					continue;

				return &classInfo;
			}
		}

		return nullptr;
	};

	auto findEnumInfo = [](const std::string& name) -> EnumInfo*
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& enumInfo : fileInfo.second.enumInfos)
			{
				if (enumInfo.name == name)
					return &enumInfo;
			}
		}

		return nullptr;
	};

	for (auto& entry : externalClassInfos)
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				if (classInfo.name != entry.first)
					continue;

				for (auto& method : entry.second.methods)
				{
					if (((int)method.flags & (int)MethodFlags::Constructor) != 0)
					{
						if (method.returnInfo.typeName.size() == 0)
						{
							outs() << "Error: Found an external constructor \"" << method.sourceName << "\" with no return value, skipping.\n";
							continue;
						}

						if (method.returnInfo.typeName != entry.first)
						{
							outs() << "Error: Found an external constructor \"" << method.sourceName << "\" whose return value doesn't match the external class, skipping.\n";
							continue;
						}
					}
					else
					{
						if (method.paramInfos.size() == 0)
						{
							outs() << "Error: Found an external method \"" << method.sourceName << "\" with no parameters. This isn't supported, skipping.\n";
							continue;
						}

						if (method.paramInfos[0].typeName != entry.first)
						{
							outs() << "Error: Found an external method \"" << method.sourceName << "\" whose first parameter doesn't "
								" accept the class its operating on. This is not supported, skipping. \n";
							continue;
						}

						method.paramInfos.erase(method.paramInfos.begin());
					}

					classInfo.methodInfos.push_back(method);
				}
			}
		}
	}

	// Resolve copydoc comment commands
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			commentParser.ResolveCopydocComments(classInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& methodInfo : classInfo.methodInfos)
				commentParser.ResolveCopydocComments(methodInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& ctorInfo : classInfo.ctorInfos)
				commentParser.ResolveCopydocComments(ctorInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& eventInfo : classInfo.eventInfos)
				commentParser.ResolveCopydocComments(eventInfo.documentation, classInfo.name, classInfo.ns);
		}

		for (auto& structInfo : fileInfo.second.structInfos)
			commentParser.ResolveCopydocComments(structInfo.documentation, structInfo.name, structInfo.ns);

		for(auto& enumInfo : fileInfo.second.enumInfos)
		{
			commentParser.ResolveCopydocComments(enumInfo.documentation, enumInfo.name, enumInfo.ns);

			for (auto& enumEntryInfo : enumInfo.entries)
				commentParser.ResolveCopydocComments(enumEntryInfo.second.documentation, enumInfo.name, enumInfo.ns);
		}
	}

	// Generate unique interop method names
	std::unordered_set<std::string> usedNames;
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			usedNames.clear();

			auto generateInteropName = [&usedNames](MethodInfo& methodInfo)
			{
				std::string interopName = methodInfo.sourceName;
				int counter = 0;
				while (true)
				{
					auto iterFind = usedNames.find(interopName);
					if (iterFind == usedNames.end())
						break;

					interopName = methodInfo.sourceName + std::to_string(counter);
					counter++;
				}

				usedNames.insert(interopName);
				methodInfo.interopName = interopName;
			};

			for (auto& methodInfo : classInfo.methodInfos)
				generateInteropName(methodInfo);

			for (auto& methodInfo : classInfo.ctorInfos)
				generateInteropName(methodInfo);

			for (auto& eventInfo : classInfo.eventInfos)
				generateInteropName(eventInfo);
		}
	}

	// Generate property infos
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for (auto& methodInfo : classInfo.methodInfos)
			{
				bool isGetter = (methodInfo.flags & (int)MethodFlags::PropertyGetter) != 0;
				bool isSetter = (methodInfo.flags & (int)MethodFlags::PropertySetter) != 0;

				if (!isGetter && !isSetter)
					continue;

				PropertyInfo propertyInfo;
				propertyInfo.name = methodInfo.scriptName;
				propertyInfo.documentation = methodInfo.documentation;
				propertyInfo.isStatic = (methodInfo.flags & (int)MethodFlags::Static);
				propertyInfo.visibility = methodInfo.visibility;
				propertyInfo.api = methodInfo.api;
				propertyInfo.style = methodInfo.style;

				if (isGetter)
				{
					propertyInfo.getter = methodInfo.interopName;
					propertyInfo.type = methodInfo.returnInfo.typeName;
					propertyInfo.typeFlags = methodInfo.returnInfo.flags;
				}
				else // Setter
				{
					propertyInfo.setter = methodInfo.interopName;
					propertyInfo.type = methodInfo.paramInfos[0].typeName;
					propertyInfo.typeFlags = methodInfo.paramInfos[0].flags;
				}

				auto iterFind = std::find_if(classInfo.propertyInfos.begin(), classInfo.propertyInfos.end(),
					[&propertyInfo](const PropertyInfo& info)
				{
					return propertyInfo.name == info.name;
				});

				if (iterFind == classInfo.propertyInfos.end())
					classInfo.propertyInfos.push_back(propertyInfo);
				else
				{
					PropertyInfo& existingInfo = *iterFind;
					if (existingInfo.type != propertyInfo.type || existingInfo.isStatic != propertyInfo.isStatic)
					{
						outs() << "Error: Getter and setter types for the property \"" << propertyInfo.name << "\" don't match. Skipping property.\n";
						continue;
					}

					if (!propertyInfo.getter.empty())
					{
						existingInfo.getter = propertyInfo.getter;

						// Prefer documentation from setter, but use getter if no other available
						if (existingInfo.documentation.brief.empty())
							existingInfo.documentation = propertyInfo.documentation;
					}
					else
					{
						existingInfo.setter = propertyInfo.setter;
						existingInfo.style = propertyInfo.style; // Always prefer style flags from the setter

						if (!propertyInfo.documentation.brief.empty())
							existingInfo.documentation = propertyInfo.documentation;
					}
				}
			}
		}
	}

	// Generate meta-data about base classes
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			if (classInfo.baseClass.empty())
				continue;

			bool isEditor = hasAPIBED(classInfo.api);
			ClassInfo* baseClassInfo = findClassInfo(classInfo.baseClass, isEditor);
			if (baseClassInfo == nullptr)
			{
				assert(false);
				continue;
			}

			baseClassInfo->flags |= (int)ClassFlags::IsBase;
			baseClassLookup[baseClassInfo->name].childClasses.push_back(classInfo.name);
		}
	}

	// Properly generate enum default values
	auto parseDefaultValue = [&](VarInfo& paramInfo)
	{
		if (paramInfo.defaultValue.empty())
			return;

		UserTypeInfo typeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

		if (typeInfo.type != ::ParsedType::Enum)
			return;

		int enumIdx = atoi(paramInfo.defaultValue.c_str());
		EnumInfo* enumInfo = findEnumInfo(paramInfo.typeName);
		if(enumInfo == nullptr)
		{
			outs() << "Error: Cannot map default value of \"" + paramInfo.name + 
				"\" to enum entry for enum type \"" + paramInfo.typeName + "\". Ignoring.";
			paramInfo.defaultValue = "";
			return;
		}

		auto iterFind = enumInfo->entries.find(enumIdx);
		if(iterFind == enumInfo->entries.end())
		{
			outs() << "Error: Cannot map default value of \"" + paramInfo.name + 
				"\" to enum entry for enum type \"" + paramInfo.typeName + "\". Ignoring.";
			paramInfo.defaultValue = "";
			return;
		}

		paramInfo.defaultValue = enumInfo->scriptName + "." + iterFind->second.scriptName;
	};

	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for(auto& methodInfo : classInfo.methodInfos)
			{
				for (auto& paramInfo : methodInfo.paramInfos)
					parseDefaultValue(paramInfo);
			}

			for (auto& ctorInfo : classInfo.ctorInfos)
			{
				for (auto& paramInfo : ctorInfo.paramInfos)
					parseDefaultValue(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
				parseDefaultValue(fieldInfo);

			for (auto& ctorInfo : structInfo.ctors)
			{
				for (auto& paramInfo : ctorInfo.params)
					parseDefaultValue(paramInfo);
			}
		}
	}

	// Find structs requiring special conversion
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
			{
				UserTypeInfo typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

				if(isArrayOrVector(fieldInfo.flags) || !(typeInfo.type == ::ParsedType::Builtin || typeInfo.type == ::ParsedType::Enum))
				{
					structInfo.requiresInterop = true;
					break;
				}
			}

			if (structInfo.requiresInterop)
				structInfo.interopName = getStructInteropType(structInfo.name);
			else
				structInfo.interopName = structInfo.name;
		}
	}

	// Mark parameters referencing complex structs and base types
	for (auto& fileInfo : outputFileInfos)
	{
		auto markComplexType = [](const std::string& type, int& flags)
		{
			UserTypeInfo typeInfo = getTypeInfo(type, flags);
			if (typeInfo.type != ::ParsedType::Struct)
				return;

			StructInfo* structInfo = findStructInfo(type);
			if (structInfo != nullptr && structInfo->requiresInterop)
				flags |= (int)TypeFlags::ComplexStruct;
		};

		auto markBaseType = [&findClassInfo](const std::string& type, int& flags)
		{
			UserTypeInfo typeInfo = getTypeInfo(type, flags);
			if (typeInfo.type != ::ParsedType::Class && typeInfo.type != ::ParsedType::ReflectableClass &&
				typeInfo.type != ::ParsedType::GUIElement && !isHandleType(typeInfo.type))
				return;

			ClassInfo* classInfo = findClassInfo(type, false);
			if (classInfo != nullptr)
			{
				bool isBase = (classInfo->flags & (int)ClassFlags::IsBase) != 0;
				if (isBase)
					flags |= (int)TypeFlags::ReferencesBase;
			}
		};

		auto markParam = [&markComplexType,&markBaseType](VarInfo& paramInfo)
		{
			markComplexType(paramInfo.typeName, paramInfo.flags);
			markBaseType(paramInfo.typeName, paramInfo.flags);
		};

		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for(auto& methodInfo : classInfo.methodInfos)
			{
				for (auto& paramInfo : methodInfo.paramInfos)
					markParam(paramInfo);

				if (methodInfo.returnInfo.typeName.size() != 0)
				{
					markComplexType(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
					markBaseType(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
				}
			}

			for (auto& eventInfo : classInfo.eventInfos)
			{
				for (auto& paramInfo : eventInfo.paramInfos)
					markParam(paramInfo);
			}

			for (auto& ctorInfo : classInfo.ctorInfos)
			{
				for (auto& paramInfo : ctorInfo.paramInfos)
					markParam(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
			{
				markComplexType(fieldInfo.typeName, fieldInfo.flags);
				markParam(fieldInfo);
			}
		}
	}

	// Generate referenced includes
	{
		for (auto& fileInfo : outputFileInfos)
		{
			IncludesInfo includesInfo;
			for (auto& classInfo : fileInfo.second.classInfos)
				GatherIncludes(classInfo, includesInfo);

			for (auto& structInfo : fileInfo.second.structInfos)
				GatherIncludes(structInfo, includesInfo);

			// Needed for all .h files
			if (!fileInfo.second.inEditor)
				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEnginePrerequisites.h");
			else
				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEditorPrerequisites.h");

			// Needed for all .cpp files
			fileInfo.second.referencedSourceIncludes.push_back("BsScript" + fileInfo.first + ".generated.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoMethod.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoClass.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoUtil.h");

			for (auto& classInfo : fileInfo.second.classInfos)
			{
				UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];

				fileInfo.second.forwardDeclarations.insert({ classInfo.ns, classInfo.cleanName, isStruct(classInfo.flags), classInfo.templParams });

				if (typeInfo.type == ::ParsedType::Resource)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptResource.h");
				else if (typeInfo.type == ::ParsedType::Component)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptComponent.h");
				else if (typeInfo.type == ::ParsedType::SceneObject)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptSceneObject.h");
				else if (typeInfo.type == ::ParsedType::GUIElement)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/GUI/BsScriptGUIElement.h");
				else if (typeInfo.type == ::ParsedType::ReflectableClass)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptReflectable.h");
				else // Class
					fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");

				if (!classInfo.baseClass.empty())
				{
					UserTypeInfo& baseTypeInfo = cppToCsTypeMap[classInfo.baseClass];

					if(hasAPIBED(classInfo.api))
						fileInfo.second.referencedHeaderIncludes.push_back(baseTypeInfo.destFileEditor);
					else
						fileInfo.second.referencedHeaderIncludes.push_back(baseTypeInfo.destFile);
				}

				if (typeInfo.type != ::ParsedType::ReflectableClass && classInfo.templParams.empty())
					fileInfo.second.referencedSourceIncludes.push_back(typeInfo.declFile);
				else
				{
					// Templated classes need to be included in header, so the linker doesn't instantiate them multiple times for different libraries
					// (in case template is exported).
					// Reflectable classes need to be included in the header because they provide a getInternal<T>() method
					// which requires information about T.
					fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.declFile);
				}
			}

			for(auto& structInfo : fileInfo.second.structInfos)
			{
				UserTypeInfo& typeInfo = cppToCsTypeMap[structInfo.name];

				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");
				fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.declFile);
			}

			if(includesInfo.requiresResourceManager)
				fileInfo.second.referencedSourceIncludes.push_back("BsScriptResourceManager.h");

			if (includesInfo.requiresRRef)
				fileInfo.second.referencedSourceIncludes.push_back("Wrappers/BsScriptRRefBase.h");

			if (includesInfo.requiresAsyncOp)
				fileInfo.second.referencedSourceIncludes.push_back("Wrappers/BsScriptAsyncOp.h");

			if(includesInfo.requiresGameObjectManager)
				fileInfo.second.referencedSourceIncludes.push_back("BsScriptGameObjectManager.h");

			if(includesInfo.requiresRTTI)
				fileInfo.second.referencedSourceIncludes.push_back("Reflection/BsRTTIType.h");

			for (auto& entry : includesInfo.includes)
			{
				uint32_t originFlags = entry.second.originIncludeFlags;
				uint32_t interopFlags = entry.second.interopIncludeFlags;

				if (originFlags != 0)
				{
					std::string include = entry.second.typeInfo.declFile;

					if ((originFlags & IT_FWD) != 0)
						fileInfo.second.forwardDeclarations.insert({ entry.second.typeInfo.ns, entry.second.typeName, entry.second.isStruct });

					if((originFlags & IT_IMPL) != 0)
						fileInfo.second.referencedSourceIncludes.push_back(include);
					else
						fileInfo.second.referencedHeaderIncludes.push_back(include);
				}

				if (interopFlags != 0)
				{
					std::string include;
					if(entry.second.isEditor)
						include = entry.second.typeInfo.destFileEditor;
					else
						include = entry.second.typeInfo.destFile;

					if ((interopFlags & IT_FWD) != 0)
					{
						if(entry.second.isEditor)
							fileInfo.second.forwardDeclarations.insert({ entry.second.typeInfo.ns, entry.second.typeName, false });
					}

					if(!include.empty())
					{
						if ((interopFlags & IT_IMPL) != 0)
							fileInfo.second.referencedSourceIncludes.push_back(include);
						else
							fileInfo.second.referencedHeaderIncludes.push_back(include);
					}
				}
			}

			for (auto& entry : includesInfo.fwdDecls)
				fileInfo.second.forwardDeclarations.insert(entry.second);
		}
	}

	// Generate overloads for unsupported default parameters
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			std::vector<MethodInfo> newMethodInfos;
			for (auto& methodInfo : classInfo.methodInfos)
				PostProcessDefaultParameters(methodInfo, newMethodInfos);

			for (auto& methodInfo : newMethodInfos)
				classInfo.methodInfos.push_back(methodInfo);

			std::vector<MethodInfo> newCtorInfos;
			for (auto& ctorInfo : classInfo.ctorInfos)
				PostProcessDefaultParameters(ctorInfo, newCtorInfos);

			for (auto& ctorInfo : newCtorInfos)
				classInfo.ctorInfos.push_back(ctorInfo);
		}
	}
}

void ParserUtility::PostProcessDefaultParameters(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos)
{
	int firstDefaultParam = -1;
	int lastInvalidParam = -1;
	for (int i = 0; i < methodInfo.paramInfos.size(); i++)
	{
		const VarInfo& param = methodInfo.paramInfos[i];

		if (!param.defaultValue.empty())
		{
			firstDefaultParam = i;
			break;
		}
	}

	for (int i = 0; i < methodInfo.paramInfos.size(); i++)
	{
		const VarInfo& param = methodInfo.paramInfos[i];

		if (!param.defaultValueType.empty() && !isFlagsEnum(param.flags))
			lastInvalidParam = i;
	}

	// Nothing to handle
	if (lastInvalidParam == -1)
		return;

	// Mark any non-complex default params as complex, so the generator doesn't generate them (since default arguments
	// must follow them, which they can't because at least one is complex)
	for (int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VarInfo& param = methodInfo.paramInfos[i];

		if (param.defaultValueType.empty())
			param.defaultValueType = "null";
	}

	// Generate a method for each default param
	for (int i = lastInvalidParam; i >= firstDefaultParam; i--)
	{
		MethodInfo copyMethodInfo = methodInfo;

		// Clear all param default values
		for (int j = firstDefaultParam; j < i; j++)
		{
			VarInfo& param = copyMethodInfo.paramInfos[j];
			param.defaultValue = "";
			param.defaultValueType = "";
		}

		// Erase docs for the params we'll skip during generation
		CommentEntry& docs = copyMethodInfo.documentation;
		for (int j = i; j <= lastInvalidParam; j++)
		{
			const std::string& paramName = copyMethodInfo.paramInfos[j].name;

			for (auto iter = docs.params.begin(); iter != docs.params.end();)
			{
				if (iter->Name == paramName)
					iter = docs.params.erase(iter);
				else
					++iter;
			}
		}

		copyMethodInfo.flags |= (int)MethodFlags::CSOnly;
		newMethodInfos.push_back(copyMethodInfo);
	}

	// Clear default params from this method
	for (int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VarInfo& param = methodInfo.paramInfos[i];
		param.defaultValue = "";
		param.defaultValueType = "";
	}
}

void ParserUtility::GatherIncludes(const std::string& typeName, int flags, bool isEditor, IncludesInfo& output)
{
	UserTypeInfo typeInfo = getTypeInfo(typeName, flags);
	if (typeInfo.type == ::ParsedType::Class || typeInfo.type == ::ParsedType::ReflectableClass ||
		typeInfo.type == ::ParsedType::Struct || typeInfo.type == ::ParsedType::Component ||
		typeInfo.type == ::ParsedType::SceneObject || typeInfo.type == ::ParsedType::Resource ||
		typeInfo.type == ::ParsedType::Enum)
	{
		auto iterFind = output.includes.find(typeName);
		if (iterFind == output.includes.end())
		{
			uint32_t sourceIncludeType = 0;
			uint32_t interopIncludeType = typeInfo.type != ::ParsedType::Enum ? IT_IMPL : 0;
			bool isStruct = false;

			if (getPassAsResourceRef(flags))
			{
				sourceIncludeType = IT_IMPL;
				interopIncludeType = 0;
			}

			if (typeInfo.type == ::ParsedType::Struct && !isComplexStruct(flags))
			{
				sourceIncludeType = IT_HEADER;
				isStruct = true;
			}

			// If enum or passed by value we need to include the header for the source type
			if (typeInfo.type == ::ParsedType::Enum || isSrcValue(flags))
				sourceIncludeType = IT_HEADER;

			// If a class is being passed by reference or a raw pointer then we need the declaration because we perform
			// assignment copy
			if (isClassType(typeInfo.type) && !isSrcSPtr(flags))
				sourceIncludeType = IT_HEADER;

			output.includes[typeName] = IncludeInfo(typeName, typeInfo, sourceIncludeType, interopIncludeType, isStruct, isEditor);
		}

		if (isClassType(typeInfo.type))
		{
			bool isBase = isBaseParam(flags);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(typeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, getTypeInfo(entry, 0), IT_IMPL, IT_IMPL, false, isEditor);

				output.requiresRTTI = true;
			}
		}
	}

	if (typeInfo.type == ::ParsedType::Struct && isComplexStruct(flags))
		output.fwdDecls[typeName] = { typeInfo.ns, getStructInteropType(typeName), true };

	if (typeInfo.type == ::ParsedType::Resource)
	{
		output.requiresResourceManager = true;

		if (getPassAsResourceRef(flags))
			output.requiresRRef = true;
	}
	else if (typeInfo.type == ::ParsedType::Component || typeInfo.type == ::ParsedType::SceneObject)
		output.requiresGameObjectManager = true;

	if (getIsAsyncOp(flags))
		output.requiresAsyncOp = true;
}

void ParserUtility::GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output)
{
	bool returnAsParameter = false;
	if (!methodInfo.returnInfo.typeName.empty())
		GatherIncludes(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags, isEditor, output);

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		GatherIncludes(I->typeName, I->flags, isEditor, output);

	if ((methodInfo.flags & (int)MethodFlags::External) != 0)
	{
		auto iterFind = output.includes.find(methodInfo.externalClass);
		if (iterFind == output.includes.end())
		{
			UserTypeInfo typeInfo = getTypeInfo(methodInfo.externalClass, 0);
			output.includes[methodInfo.externalClass] = IncludeInfo(methodInfo.externalClass, typeInfo, IT_FWD_AND_IMPL, 0, false, isEditor);
		}
	}
}

void ParserUtility::GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output)
{
	UserTypeInfo fieldTypeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

	// These types never require additional includes
	if (fieldTypeInfo.type == ::ParsedType::Builtin || fieldTypeInfo.type == ::ParsedType::String ||
		fieldTypeInfo.type == ::ParsedType::WString || fieldTypeInfo.type == ::ParsedType::Path)
		return;

	// If passed by value, we needs its header in our header
	if (isSrcValue(fieldInfo.flags))
	{
		bool complexStruct = isComplexStruct(fieldInfo.flags);

		output.includes[fieldInfo.typeName] = IncludeInfo(fieldInfo.typeName, fieldTypeInfo, IT_HEADER, complexStruct ? IT_HEADER : 0, false, isEditor);
	}

	if (fieldTypeInfo.type == ::ParsedType::Class || fieldTypeInfo.type == ::ParsedType::ReflectableClass ||
		fieldTypeInfo.type == ::ParsedType::Struct || fieldTypeInfo.type == ::ParsedType::Component ||
		fieldTypeInfo.type == ::ParsedType::SceneObject || fieldTypeInfo.type == ::ParsedType::Resource)
	{
		bool isRRef = getPassAsResourceRef(fieldInfo.flags);

		if (!fieldTypeInfo.destFile.empty() || isRRef)
		{
			std::string name = "__" + fieldInfo.typeName;
			output.includes[name] = IncludeInfo(fieldInfo.typeName, fieldTypeInfo, IT_IMPL, IT_IMPL, false, isEditor);
		}

		if (fieldTypeInfo.type == ::ParsedType::Resource)
		{
			output.requiresResourceManager = true;

			if (getPassAsResourceRef(fieldInfo.flags))
				output.requiresRRef = true;
		}
		else if (fieldTypeInfo.type == ::ParsedType::Component || fieldTypeInfo.type == ::ParsedType::SceneObject)
			output.requiresGameObjectManager = true;
		else if (fieldTypeInfo.type == ::ParsedType::Class || fieldTypeInfo.type == ::ParsedType::ReflectableClass)
		{
			bool isBase = isBaseParam(fieldInfo.flags);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(fieldInfo.typeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, getTypeInfo(entry, 0), IT_IMPL, IT_IMPL, false, isEditor);

				output.requiresRTTI = true;
			}
		}

		if (getIsAsyncOp(fieldInfo.flags))
			output.requiresAsyncOp = true;
	}
}

void ParserUtility::GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output)
{
	bool isEditor = hasAPIBED(classInfo.api);

	for (auto& methodInfo : classInfo.ctorInfos)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& methodInfo : classInfo.methodInfos)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& eventInfo : classInfo.eventInfos)
		GatherIncludes(eventInfo, isEditor, output);
}

void ParserUtility::GatherIncludes(const StructInfo& structInfo, IncludesInfo& output)
{
	bool isEditor = hasAPIBED(structInfo.api);

	if (structInfo.requiresInterop)
	{
		for (auto& fieldInfo : structInfo.fields)
			GatherIncludes(fieldInfo, isEditor, output);
	}
}
