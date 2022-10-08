#include "B3DCommon.h"
#include <chrono>

#include "B3DCommentParser.h"
#include "B3DParserUtility.h"

/**
 * Returns true if dereferencing is required when passing this type from/to script.
 *
 * @param	typeInformation				Information about the native type to generate the interop type for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 * @return								True if the type should be dereferenced.
 */
static bool IsDereferenceRequired(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	// Other types aren't allowed to be dereferenced
	if (typeMappingInformation.TypeCategory != ExportedClassTypeCategory::Class && typeMappingInformation.TypeCategory != ExportedClassTypeCategory::ReflectableClass)
		return false;

	return typeInformation.TypeCategory != VariableTypeCategory::SharedPointer;
}

/** Returns true if the provided type is passed as value type to an internal method parameter. */
static bool IsInternalMethodParameterValueType(const VariableTypeInformation& typeInformation)
{
	if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
		return false;

	switch(typeInformation.TypeCategory)
	{
	case VariableTypeCategory::SharedPointer:
	case VariableTypeCategory::ResourceHandle: 
	case VariableTypeCategory::GameObjectHandle:
	case VariableTypeCategory::MonoObject:
		return false;
	default: 
		return true;
	}
}

/**
 * Returns a qualified name for the C++ interop type representing the type in @p typeInformation.
 *
 * @param	typeInformation				Information about the native type to generate the interop type for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 * @param	isGeneratingField			When true, it implies the interop type will be used for generating a field member in class or struct. If false it implies we're generating it for a parameter or a return value.
 */
static std::string GetCppInteropQualifiedTypeName(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool isGeneratingField = false)
{
	const bool isOutputParameter = typeInformation.IsOutputParameter() && !isGeneratingField;
	if (typeInformation.IsArrayOrVector())
		return isOutputParameter ? "MonoArray**" : "MonoArray*";

	const std::string& typeName = typeInformation.GetFirstWrappedOrSelfTypeName();

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive:
		return isOutputParameter ? typeName + "*" : typeName;
	case ExportedClassTypeCategory::Enum:
		if (typeInformation.TypeCategory == VariableTypeCategory::Flags && isGeneratingField)
			return "Flags<" + typeName + ">";
		else
			return isOutputParameter ? typeName + "*" : typeName;
	case ExportedClassTypeCategory::Struct:
		if(typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
		{
			const std::string structInteropType = GetStructInteropTypeName(typeName);
			return isGeneratingField ? structInteropType : structInteropType + "*";
		}

		return isGeneratingField ? typeName : typeName + "*";
	case ExportedClassTypeCategory::String:
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::Path:
		return isOutputParameter ? "MonoString**" : "MonoString*";
	default: // Class, resource, component or ScriptObject
		return isOutputParameter ? "MonoObject**" : "MonoObject*";
	}
}

/**
 * Returns a type name for the C++ native type representing the type in @p typeInformation.
 *
 * @param	typeInformation					Information about the native type to generate the type name for.
 * @param	typeMappingInformation			Mapping of the provided type in script.
 * @param	isVariable						If true, the generated type is expected to be used for local variable storage (e.g. qualifiers such as `const &` will be dropped.). Otherwise it's expected to be used for parameters or template arguments.
 */
static std::string GetCppNativeQualifiedTypeName(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool isVariable = true)
{
	const std::string& typeName = typeInformation.GetFirstWrappedOrSelfTypeName();

	std::stringstream output;
	if (!isVariable && typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsConst))
		output << "const ";

	if (typeInformation.TypeCategory == VariableTypeCategory::Vector)
		output << "Vector<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ">";
	else if (typeInformation.TypeCategory == VariableTypeCategory::SmallVector)
		output << "SmallVector<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ", " + std::to_string(typeInformation.ArraySize) + ">";
	else if (typeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
		output << "TAsyncOp<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ">";
	else if (typeInformation.TypeCategory == VariableTypeCategory::Array || typeInformation.TypeCategory == VariableTypeCategory::ComponentOrActor)
		output << GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false);
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
		output << "ResourceHandle<" + typeName + ">";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject || typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
		output << "GameObjectHandle<" + typeName + ">";
	else if (isClassType(typeMappingInformation.TypeCategory))
	{
		if (isVariable || typeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
			output << "SPtr<" + typeName + ">";
		else
			output << typeName;
	}
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::String)
		output << "String";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::WString)
		output << "WString";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Path)
		output << "Path";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum && typeInformation.TypeCategory == VariableTypeCategory::Flags)
		output << "Flags<" + typeName + ">";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement)
		output << typeName + "*";
	else
		output << typeName;

	if (!isVariable)
	{
		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			output << "*";
		else if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
			output << "&";
	}

	return output.str();
}

/** Same as GetCppQualifiedTypeName, except the only type information used is the type name. */
static std::string GetCppNativeQualifiedTypeName(const std::string& typeName, const TypeMappingInformation& typeMappingInformation, bool wrapClassTypesInSharedPointer = true)
{
	VariableTypeInformation typeInformation;
	typeInformation.TypeName = typeName;

	return GetCppNativeQualifiedTypeName(typeInformation, typeMappingInformation, wrapClassTypesInSharedPointer);
}

/**
 * Returns a type name for the Mono thunk signature lookup, representing the type in @p typeInformation.
 *
 * @param	typeInformation					Information about the native type to generate the type name for.
 * @param	typeMappingInformation			Mapping of the provided type in script.
 */
static std::string GetInteropThunkSignatureQualifiedTypeName(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	std::string typeName;

	// Generic types require `X after their name
	StringRef inputStr(typeMappingInformation.ScriptTypeName.data(), typeMappingInformation.ScriptTypeName.length());
	inputStr = inputStr.trim();

	const size_t leftBracketIdx = inputStr.find_first_of('<');
	const size_t rightBracketIdx = inputStr.find_last_of('>');
	const size_t leftBracketCount = inputStr.count('<');
	const size_t rightBracketCount = inputStr.count('>');

	if (leftBracketCount > 1 || rightBracketCount > 1)
	{
		outs() << "Error: Cannot parse event signature type. Nested generic parameters are not allowed.\n";
		typeName = typeMappingInformation.ScriptTypeName;
	}
	else if (leftBracketIdx != StringRef::npos && rightBracketIdx != StringRef::npos)
	{
		StringRef templateType = inputStr.substr(0, leftBracketIdx);
		StringRef templateArgs = inputStr.substr(leftBracketIdx + 1, rightBracketIdx - leftBracketIdx - 1);
		const size_t templateArgumentCount = templateArgs.count(',') + 1;

		typeName = templateType.str() + "`" + std::to_string(templateArgumentCount) + "<" + templateArgs.str() + ">";
	}
	else
		typeName = typeMappingInformation.ScriptTypeName;

	if(typeName == "float")
		typeName = "single";

	std::stringstream output;

	output << typeName;

	if (typeInformation.IsArrayOrVector())
		output << "[]";

	if (typeInformation.IsOutputParameter() || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
		output << "&";

	return output.str();
}

/**
 * Generates a line of code that retrieves the underlying (internal) object from the script interop object.
 *
 * @param typeInformation			Information about the native type the internal object represents.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @param variableName				Name of the variable containing the script interop object, to access the underlying object through.
 * @return							String containing the C++ line of code to retrieve the underlying (internal) object from @p variableName.
 */
static std::string GenerateGetInternalCallLine(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& variableName)
{
	const bool isPassingAsResourceReference = typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
	const bool isReferencingBaseClass = typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);

	const std::string& nativeTypeName = typeInformation.GetLastWrappedOrSelfTypeName();

	std::stringstream output;
	if (isClassType(typeMappingInformation.TypeCategory))
		output << variableName << "->GetInternal()";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
		output << "static_cast<" << nativeTypeName << "*>(" << variableName << "->GetGuiElement())";
	else // Must be one of the handle types
	{
		assert(isHandleType(typeMappingInformation.TypeCategory));

		if (!isReferencingBaseClass || isPassingAsResourceReference)
		{
			if(isPassingAsResourceReference)
				output << "static_resource_cast<" << nativeTypeName << ">(" << variableName << "->GetHandle())";
			else
			{
				if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource && nativeTypeName == "Resource")
					output << "static_resource_cast<" << nativeTypeName << ">(" << variableName << "->GetGenericHandle())";
				else
					output << variableName << "->GetHandle()";
			}
		}
		else
		{
			if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
				output << "static_resource_cast<" << nativeTypeName << ">(" << variableName << "->GetGenericHandle())";
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
				output << "static_object_cast<" << nativeTypeName << ">(" << variableName << "->GetComponent())";
		}
	}
	
	return output.str();
}

/**
 * Returns an argument that can be used for call into a native method. The argument is expected to have been received through
 * an internal interop call.
 *
 * @param	methodInfo				Information about the method being called.
 * @param	argumentName			Name of the argument.
 * @param	typeInformation			Information about the native type the argument represents.
 * @param	typeMappingInformation	Mapping of the provided argument type in script.
 * @return							Code to retrieves the appropriate argument type from the expected internal argument storage type.
 */
static std::string GetArgumentForInternalToNativeCall(const MethodInfo& methodInfo, const std::string& argumentName, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	// Performs the conversion for arguments whose source type is either a value type or pointer type.
	auto fnGetPlainArgument = [&methodInfo, &argumentName, &typeInformation](bool isInputPointerType)
	{
		if (IsInternalMethodParameterValueType(typeInformation))
			return (isInputPointerType ? "*" : "") + argumentName;

		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return (isInputPointerType ? "" : "&") + argumentName;

		errs() << "Error: Invalid type for method argument " << argumentName << " on method " << methodInfo.sourceName << ".\n";
		return argumentName;
	};

	enum class HandleType
	{
		ResourceHandle,
		GameObjectHandle
	};

	// Performs the conversion for arguments whose source type is a handle type.
	auto fnGetHandleArgument = [&argumentName, &typeInformation](HandleType handleType)
	{
		if (handleType == HandleType::ResourceHandle)
		{
			assert(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::ResourceHandle || typeInformation.TypeCategory == VariableTypeCategory::General);

			if (typeInformation.TypeCategory == VariableTypeCategory::ResourceHandle)
				return argumentName;
		}
		else
		{
			assert(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::GameObjectHandle || typeInformation.TypeCategory == VariableTypeCategory::ComponentOrActor || typeInformation.TypeCategory == VariableTypeCategory::General);

			if (typeInformation.TypeCategory == VariableTypeCategory::GameObjectHandle || typeInformation.TypeCategory == VariableTypeCategory::ComponentOrActor)
				return argumentName;
		}

		if (typeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
			return argumentName + ".GetInternalPtr()";

		assert(typeInformation.TypeCategory == VariableTypeCategory::General);
		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return argumentName + ".get()";

		return "*" + argumentName;
	};

	if(typeInformation.IsArrayOrVector())
	{
		return fnGetPlainArgument(typeInformation.IsOutputParameter());
	}

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive:
	case ExportedClassTypeCategory::Enum: // Input type is either value or pointer depending if output or not
		return fnGetPlainArgument(typeInformation.IsOutputParameter());
	case ExportedClassTypeCategory::Struct: // Input type is always a pointer
		return fnGetPlainArgument(!typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed));
	case ExportedClassTypeCategory::MonoObject: // Input type is either a pointer or a pointer to pointer, depending if output or not
		return fnGetPlainArgument(typeInformation.IsOutputParameter());
	case ExportedClassTypeCategory::String: // Input type is always a value
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::Path:
		return fnGetPlainArgument(false);
	case ExportedClassTypeCategory::GUIElement: // Input type is always a pointer
		return fnGetPlainArgument(true);
	case ExportedClassTypeCategory::Component: // Input type is always a handle
	case ExportedClassTypeCategory::SceneObject:
		return fnGetHandleArgument(HandleType::GameObjectHandle);
	case ExportedClassTypeCategory::Resource:
		return fnGetHandleArgument(HandleType::ResourceHandle);
	case ExportedClassTypeCategory::Class: // Input type is always a SPtr
	case ExportedClassTypeCategory::ReflectableClass:
	{
		assert(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::ComponentOrActor || typeInformation.TypeCategory == VariableTypeCategory::General);

		if(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::ComponentOrActor)
			return argumentName;

		if(typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return argumentName + ".get()";

		return "*" + argumentName;
	}
	default: // Some object type
		assert(false);
		return "";
	}
}

/*
 * Returns an argument that can be used for call into a thunk. The argument is expected to have been received through a native event, translated to an interop type.
 *
 * @param	methodInfo				Information about the event that is being triggered.
 * @param	argumentName			Name of the argument.
 * @param	typeInformation			Information about the native type the argument represents.
 * @param	typeMappingInformation	Mapping of the provided argument type in script.
 * @return							Code to retrieves the appropriate argument type from the event parameter argument type.
 */
static std::string GetArgumentForInteropEventToThunkCall(const MethodInfo& methodInfo, const std::string& argumentName, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	if(typeInformation.IsArrayOrVector())
	{
		// Always passed as pointer (MonoArray*), input will always be a pointer
		return argumentName;
	}

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive:
	case ExportedClassTypeCategory::Enum: // Always passed as value type, input can be either pointer or ref/value type
	{
		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return "*" + argumentName;

		return argumentName;
	}
	case ExportedClassTypeCategory::Struct: // Always passed as pointer, input will be a pointer (boxed struct as MonoObject*)
	case ExportedClassTypeCategory::MonoObject: // Always passed as a pointer, input will always be a pointer (MonoObject*)
	case ExportedClassTypeCategory::String:
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::Path:
	case ExportedClassTypeCategory::Component:
	case ExportedClassTypeCategory::SceneObject:
	case ExportedClassTypeCategory::Resource:
	case ExportedClassTypeCategory::Class:
	case ExportedClassTypeCategory::ReflectableClass:
		return argumentName;
	default: // Some object type
		errs() << "Error: Invalid type for method argument " << argumentName << " on method " << methodInfo.sourceName << ".\n";
		return argumentName;
	}
}

/*
 * Adds appropriate qualifiers to convert a type access (such as field or method call return value) into a default storage type for the mapped type category.
 *
 * @param	access					Field name or method call.
 * @param	typeInformation			Information about the native type the argument represents.
 * @param	typeMappingInformation	Mapping of the provided argument type in script.
 * @return							Code to converts the field or return value into the expected internal argument storage type.
 */
static std::string GetReturnValueForNativeCall(const std::string& access, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	const VariableTypeInformation& arrayElementType = typeInformation.IsArrayOrVector() ? typeInformation.AssertGetUnderlyingType() : typeInformation;
	const VariableTypeInformation& underlyingType = arrayElementType.TypeCategory == VariableTypeCategory::AsyncOp ? arrayElementType.AssertGetUnderlyingType() : arrayElementType;

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive: // Always passed as value type, input can be either pointer or ref/value type
	case ExportedClassTypeCategory::Enum:
	case ExportedClassTypeCategory::String:
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::Path:
	case ExportedClassTypeCategory::Struct:
	{
		if (underlyingType.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return "*" + access;

		return access;
	}
	case ExportedClassTypeCategory::MonoObject: // Always passed as a pointer, input must always be a pointer
	case ExportedClassTypeCategory::GUIElement:
			return access;
	case ExportedClassTypeCategory::Component: // Always passed as a handle, input must be a handle
	{
		const VariableTypeInformation& componentOrActorUnderlyingType = underlyingType.TypeCategory == VariableTypeCategory::ComponentOrActor ? underlyingType.AssertGetUnderlyingType() : underlyingType;
		if (componentOrActorUnderlyingType.TypeCategory != VariableTypeCategory::GameObjectHandle)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		if (underlyingType.TypeCategory == VariableTypeCategory::ComponentOrActor)
			return access + ".GetComponent()";

		return access;
	}
	case ExportedClassTypeCategory::SceneObject:
		if (underlyingType.TypeCategory != VariableTypeCategory::GameObjectHandle)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		return access;
	case ExportedClassTypeCategory::Resource:
		if (underlyingType.TypeCategory != VariableTypeCategory::ResourceHandle)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		return access;
	case ::ExportedClassTypeCategory::Class: // Passed as a shared pointer or value type, input can be a shared pointer, pointer, reference or value type
	case ::ExportedClassTypeCategory::ReflectableClass:
	{
		if (underlyingType.TypeCategory == VariableTypeCategory::SharedPointer)
			return access;

		if(underlyingType.TypeCategory != VariableTypeCategory::General)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		if (underlyingType.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return "*" + access;

		return access;
	}
	default: // Some object type
		assert(false);
		return "";
	}
}

/**
 * Returns the name of the script interop type used for the provided type name
 *
 * @param typeName					Type name of the type to look up.
 * @param isResourceReference	If the type is a resource, this will return a resource reference script interop class, rather than the resource's own interop class.
 * @return						Name of the type used for script interop for the provided type name.
 */
static std::string GetScriptInteropTypeName(const std::string& typeName, bool isResourceReference = false)
{
	auto iterFind = NativeToScriptTypeMap.find(typeName);
	if (iterFind == NativeToScriptTypeMap.end())
		outs() << "Warning: Type \"" << typeName << "\" referenced as a script interop type, but no script interop mapping found. Assuming default type name.\n";

	bool isValidInteropType = iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Primitive &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Enum &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::String &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::WString &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Path;

	if (!isValidInteropType)
		outs() << "Error: Type \"" << typeName << "\" referenced as a script interop type, but script interop object cannot be generated for this object type.\n";

	std::string cleanName = CleanTemplateParameters(typeName);

	if(isResourceReference)
	{
		if(iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Resource)
			outs() << "Error: Type \"" << typeName << "\" cannot be wrapped in a resource reference.\n";

		return "ScriptRRefBase";
	}
	
	return "Script" + cleanName;
}

/** Generates a check for a preprocessor conditional depending on the API the code is currently being compiled for. */
static std::string GenerateApiCheckBegin(ApiFlags api)
{
	if(api == ApiFlags::Framework)
		return "#if !BS_IS_BANSHEE3D\n";
	else if(api == ApiFlags::Engine)
		return "#if BS_IS_BANSHEE3D\n";

	return "";
}

/** Ends the preprocessor conditional started by GenerateAPICheckBegin(). These calls must match 1:1. */
static std::string GenerateApiCheckEnd(ApiFlags api)
{
	if(api == ApiFlags::Framework || api == ApiFlags::Engine)
		return "#endif\n";

	return "";
}

/**
 * Generates the name and parameters for an internal method.
 *
 * @param methodInfo			Information about the method being generated.
 * @param interopThisPtrType	Interop type used for storing the interop object we're generating the method for. This may be the same as @p interopTypeName, but may be some base type.
 * @param interopTypeName		Interop type we're generating the method on. This will be used as a prefix to the method name, followed by '::'. Set to empty if generating signature for the header declaration.
 * @param isModule				True if the type is Module singleton.
 * @return						Method signature, including method name and parameters.
 */
static std::string GenerateInternalMethodSignature(const MethodInfo& methodInfo, const std::string& interopThisPtrType, const std::string& interopTypeName, bool isModule)
{
	const bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	const bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInfo.returnInfo.TypeInformation.TypeName.empty() || isCtor)
		output << "void";
	else
	{
		const TypeMappingInformation& returnTypeMappingInformation = GetNativeToScriptTypeMapping(methodInfo.returnInfo.TypeInformation);
		if (!CanBeReturned(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			output << GetCppInteropQualifiedTypeName(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation);
		}
	}

	output << " ";

	if (!interopTypeName.empty())
		output << interopTypeName << "::";

	output << "Internal" << methodInfo.interopName << "(";

	if (isCtor)
	{
		output << "MonoObject* managedInstance";

		if (!methodInfo.paramInfos.empty())
			output << ", ";
	}
	else if (!isStatic && !isModule)
	{
		output << interopThisPtrType << "* thisPtr";

		if (!methodInfo.paramInfos.empty() || returnAsParameter)
			output << ", ";
	}

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const TypeMappingInformation& parameterTypeMappingInformation = GetNativeToScriptTypeMapping(I->TypeInformation);
		output << GetCppInteropQualifiedTypeName(I->TypeInformation, parameterTypeMappingInformation) << " " << I->Name;

		if ((I + 1) != methodInfo.paramInfos.end() || returnAsParameter)
			output << ", ";
	}

	if (returnAsParameter)
	{
		const TypeMappingInformation& returnTypeMappingInformation = GetNativeToScriptTypeMapping(methodInfo.returnInfo.TypeInformation);
		output << GetCppInteropQualifiedTypeName(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation) << " " << "__output";
	}

	output << ")";
	return output.str();
}

/**
 * Generates the name and parameters for a method that serves as an event callback.
 *
 * @param eventInfo			Information about the event we're generating the callback for.
 * @param interopTypeName	Interop type we're generating the method on. This will be used as a prefix to the method name, followed by '::'. Set to empty if generating signature for the header declaration.
 * @param isModule			True if the type is a Module singleton.
 * @return					Method signature, including method name and parameters.
 */
static std::string GenerateEventCallbackSignature(const MethodInfo& eventInfo, const std::string& interopTypeName, bool isModule)
{
	const bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	std::stringstream output;

	if ((isStatic || isModule) && interopTypeName.empty())
		output << "static ";

	output << "void ";
	
	if (!interopTypeName.empty())
		output << interopTypeName << "::";
	
	output << eventInfo.interopName << "(";

	int parameterIndex = 0;
	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(I->TypeInformation);
		output << GetCppNativeQualifiedTypeName(I->TypeInformation, parameterTypeMappingInformation, false);

		output << " p" << parameterIndex;

		if (I->TypeInformation.TypeCategory == VariableTypeCategory::Array)
			output << "[" << I->arraySize << "]";

		if ((I + 1) != eventInfo.paramInfos.end())
			output << ", ";

		parameterIndex++;
	}

	output << ")";
	return output.str();
}

/**
 * Generates the type definition and a static field holding a thunk for a particular event.
 *
 * @param eventInfo			Information about the event we're generating the thunk for.
 * @param isModule			True if the type the event is on is a Module singleton.
 * @return					Thunk type definition, followed by thunk static field.
 */
std::string GenerateEventThunkSignature(const MethodInfo& eventInfo, bool isModule)
{
	const bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	std::stringstream output;
	output << "\t\ttypedef void(BS_THUNKCALL *" << eventInfo.sourceName << "ThunkDef) (";
	
	if (!isStatic && !isModule)
		output << "MonoObject*, ";

	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(I->TypeInformation);

		if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct)
			output << "MonoObject* " << I-> Name << ", ";
		else
			output << GetCppInteropQualifiedTypeName(I->TypeInformation, parameterTypeMappingInformation) << " " << I->Name << ", ";
	}

	output << "MonoException**);" << std::endl;
	output << "\t\tstatic " << eventInfo.sourceName << "ThunkDef " << eventInfo.sourceName << "Thunk;" << std::endl;

	return output.str();
}

/**
 * Returns code that converts a Mono object into its script interop type.
 *
 * @param indent					Indent to apply to the generated line of code.
 * @param scriptType				Script interop type.
 * @param scriptName				Name of the script interop variable to store the result in.
 * @param variableName				Name of the variable containing the Mono object.
 * @param typeInformation			Information about the native type.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @return							ode that converts a Mono object into its script interop type, assigning it to a variable named @p scriptName.
 */
static std::string GenerateMonoToScriptObject(const std::string& indent, const std::string& scriptType, const std::string& scriptName, const std::string& variableName, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	const bool isRRef = typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
	const bool isBase = typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);

	std::stringstream output;
	if (!isBase || isRRef)
	{
		output << indent << scriptType << "* " << scriptName << ";" << std::endl;
		output << indent << scriptName << " = " << scriptType << "::ToNative(" << variableName << ");" << std::endl;
	}
	else
	{
		std::string scriptBaseType;
		if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
			scriptBaseType = "ScriptGUIElementBaseTBase";
		else
			scriptBaseType = scriptType + "Base";

		output << indent << scriptBaseType << "* " << scriptName << ";" << std::endl;
		output << indent << scriptName << " = (" << scriptBaseType << "*)" << scriptType << "::ToNative(" << variableName << ");" << std::endl;
	}

	return output.str();
}

/**
 * Converts a native class type argument into its script interop object. This should be only called on types that are exported to scripting a ExportedClassTypeCategory::Class or ExportedClassTypeCategory::ReflectableClass.
 *
 * @param typeInformation		Information about the native type to convert.
 * @param outputName			Name of the variable to store the result in.
 * @param scriptType			Interop script type we're doing the conversion for.
 * @param argName				Name of the variable that's being converted.
 * @param performReferenceCopy	If true, reference copy operation will be performed when assigning the value to output. Required if writing the output as an output parameter to an internal method.
 * @param indent				Optional indent to apply to the generated code.
 * @return						Code that converts a native object to a script interop object.
 */
std::string GenerateNativeClassToScriptObject(const VariableTypeInformation& typeInformation, const std::string& outputName, 
	const std::string& scriptType, const std::string& argName, bool performReferenceCopy = false, const std::string& indent = "\t\t")
{
	std::stringstream output;

	// TODO - Need to modify this part if we wish to have persistence with managed objects
	auto fnGenerateCreateLine = [&output, &outputName, performReferenceCopy](const std::string& scriptType, const std::string& argName, const std::string& indent)
	{
		if (performReferenceCopy)
			output << indent << "MonoUtil::ReferenceCopy(" << outputName << ", " << scriptType << "::Create(" << argName << "));\n";
		else
			output << indent << outputName << " = " << scriptType << "::Create(" << argName << ");\n";
	};

	if(typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass))
	{
		std::vector<std::string> derivedClasses;
		getDerivedClasses(typeInformation.GetLastWrappedOrSelfTypeName(), derivedClasses);

		if(!derivedClasses.empty())
		{
			output << indent << "if(" << argName << ")\n";
			output << indent << "{\n";

			output << indent << "\tif(rtti_is_of_type<" << derivedClasses[0] << ">(" << argName << "))\n";
			fnGenerateCreateLine(GetScriptInteropTypeName(derivedClasses[0]), "std::static_pointer_cast<" + derivedClasses[0] + ">(" + argName + ")", indent + "\t\t");

			for(uint32_t i = 1; i < (uint32_t)derivedClasses.size(); i++)
			{
				output << indent << "\telse if(rtti_is_of_type<" << derivedClasses[i] << ">(" << argName << "))\n";
				fnGenerateCreateLine(GetScriptInteropTypeName(derivedClasses[i]), "std::static_pointer_cast<" + derivedClasses[i] + ">(" + argName + ")", indent + "\t\t");
			}

			output << indent << "\telse\n";
			fnGenerateCreateLine(scriptType, argName, indent + "\t\t");


			output << indent << "}\n";
			output << indent << "else\n";
			fnGenerateCreateLine(scriptType, argName, indent + "\t");

			return output.str();
		}
	}
	else
		fnGenerateCreateLine(scriptType, argName, indent);

	return output.str();
}

// TODO - Clean up
std::string GenerateNativeHandleToScriptObject(::ExportedClassTypeCategory type, int flags, const std::string& scriptName, const std::string& argName, const std::string& indent = "\t\t")
{
	std::stringstream output;

	if (type == ::ExportedClassTypeCategory::Resource)
	{
		if(getPassAsResourceRef(flags))
		{
			output << indent << "ScriptRRefBase* " << scriptName << ";\n";
			output << indent << scriptName << " = ScriptResourceManager::Instance().GetScriptRRef(" << argName << ");\n";
		}
		else
		{
			output << indent << "ScriptResourceBase* " << scriptName << ";\n";
			output << indent << scriptName << " = ScriptResourceManager::Instance().GetScriptResource(" << argName
				<< ", true);\n";
		}
	}
	else if (type == ::ExportedClassTypeCategory::Component)
	{
		output << indent << "ScriptComponentBase* " << scriptName << " = nullptr;\n";
		output << indent << "if(" << argName << ")\n";
		output << indent << "\t" << scriptName << " = ScriptGameObjectManager::Instance().GetBuiltinScriptComponent(" <<
			"static_object_cast<Component>(" << argName << "));\n";
	}
	else if (type == ::ExportedClassTypeCategory::SceneObject)
	{
		output << indent << "ScriptSceneObject* " << scriptName << " = nullptr;\n";
		output << indent << "if(" << argName << ")\n";
		output << indent << scriptName << " = ScriptGameObjectManager::Instance().GetOrCreateScriptSceneObject(" <<
			argName << ");\n";
	}
	else
		assert(false);

	return output.str();
}

/**
 * Generates code required for passing an argument to an interop method.
 *
 * @param parameterName				Name of the parameter variable.
 * @param parameterInformation		Information about the parameter the argument is being passed to.
 * @param isLast					True if this is the last argument passed to the method/function.
 * @param returnValue				True if the argument is in fact a return value.
 * @param preCallActions			Actions to execute before the method/function call.
 * @param postCallActions			Actions to execute after the method/function call.
 * @return							Name of the argument.
 */
std::string GenerateMethodBodyBlockForArgument(const std::string& parameterName, const VariableBase& parameterInformation, bool isLast, bool returnValue, std::stringstream& preCallActions, std::stringstream& postCallActions)
{
	TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);

	if(parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
	{
		const VariableTypeInformation& asyncOpUnderlyingTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		if (!parameterInformation.TypeInformation.IsOutputParameter() && !returnValue)
		{
			outs() << "Error: AsyncOp type not supported as input parameter. \n";
			return "";
		}

		if (parameterTypeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::ReflectableClass && parameterTypeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::Class && parameterTypeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::Resource)
		{
			outs() << "Error: Type not supported as an AsyncOp return value. \n";
			return "";
		}

		std::string argumentType;
		std::string argumentName;
		if (!asyncOpUnderlyingTypeInformation.IsArrayOrVector())
		{
			argumentName = "tmp" + parameterName;
			const std::string asyncOpType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
			argumentType = GetCppNativeQualifiedTypeName(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation);

			preCallActions << "\t\t" << asyncOpType << " " << argumentName << ";\n";
		}
		else
		{
			argumentType = GetCppNativeQualifiedTypeName(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation, false);
			argumentName = "vec" + parameterName;

			preCallActions << "\t\t" << argumentType << " " << argumentName;
			if (asyncOpUnderlyingTypeInformation.TypeCategory == VariableTypeCategory::Array)
				preCallActions << "[" << asyncOpUnderlyingTypeInformation.ArraySize << "]";
			preCallActions << ";\n";
		}

		std::string monoType;
		if(asyncOpUnderlyingTypeInformation.GetLastWrappedOrSelfTypeName() != "Any")
		{
			std::string scriptType = GetScriptInteropTypeName(asyncOpUnderlyingTypeInformation.GetLastWrappedOrSelfTypeName(),
				parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource && asyncOpUnderlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));

			monoType = scriptType + "::GetMetaData()->ScriptClass";

			postCallActions << "\t\tauto convertCallback = [](const Any& returnVal)\n";
			postCallActions << "\t\t{\n";
			postCallActions << "\t\t\t" << argumentType << " nativeObj = any_cast<" << argumentType << ">(returnVal);\n";
			postCallActions << "\t\t\tMonoObject* monoObj;\n";

			if (!asyncOpUnderlyingTypeInformation.IsArrayOrVector())
			{
				if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass || parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class)
					postCallActions << GenerateNativeClassToScriptObject(asyncOpUnderlyingTypeInformation, "monoObj", scriptType, "nativeObj", false, "\t\t\t");
				else // Resource
				{
					postCallActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, parameterInformation.flags, "scriptObj", "nativeObj", "\t\t\t");
					postCallActions << "\t\t\tif(scriptObj != nullptr)" << std::endl;
					postCallActions << "\t\t\t\tmonoObj = scriptObj->GetManagedInstance();" << std::endl;
					postCallActions << "\t\t\telse" << std::endl;
					postCallActions << "\t\t\t\tmonoObj = nullptr;" << std::endl;
				}

			}
			else
			{
				const std::string arrayName = "scriptArray";

				postCallActions << "\t\t\tint arraySize = ";
				if (asyncOpUnderlyingTypeInformation.TypeCategory == VariableTypeCategory::Vector || asyncOpUnderlyingTypeInformation.TypeCategory == VariableTypeCategory::SmallVector)
					postCallActions << "(int)" << argumentName << ".size()";
				else
					postCallActions << asyncOpUnderlyingTypeInformation.ArraySize;
				postCallActions << ";\n";

				postCallActions << "\t\t\tScriptArray " << arrayName;
				postCallActions << " = " << "ScriptArray::Create<" << scriptType << ">(arraySize);" << std::endl;
				postCallActions << "\t\t\tfor(int i = 0; i < arraySize; i++)" << std::endl;
				postCallActions << "\t\t\t{" << std::endl;

				const VariableTypeInformation& arrayElementTypeInformation = asyncOpUnderlyingTypeInformation.AssertGetUnderlyingType();

				switch (parameterTypeMappingInformation.TypeCategory)
				{
				case ::ExportedClassTypeCategory::ReflectableClass:
				case ::ExportedClassTypeCategory::Class:
				{
					const std::string arrayElementName = "arrayElem" + parameterName;

					const std::string arrayElementPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
					const std::string arrayElementPtrName = "arrayElemPtr" + parameterName;

					postCallActions << "\t\t\t\t" << arrayElementPtrType << " " << arrayElementPtrName;
					if (IsDereferenceRequired(arrayElementTypeInformation, parameterTypeMappingInformation))
					{
						postCallActions << " = bs_shared_ptr_new<" << parameterInformation.TypeInformation.TypeName << ">();\n";

						if (arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
						{
							postCallActions << "\t\t\t\tif(nativeObj[i])\n";
							postCallActions << "\t\t\t\t\t*" << arrayElementPtrName << " = *";
						}
						else
						{
							postCallActions << "\t\t\t\t*" << arrayElementPtrName << " = ";
						}

						postCallActions << "nativeObj[i];\n";
					}
					else
						postCallActions << " = nativeObj[i];\n";

					postCallActions << "\t\t\t\tMonoObject* " << arrayElementName << ";\n";
					postCallActions << GenerateNativeClassToScriptObject(arrayElementTypeInformation, arrayElementName, scriptType, arrayElementPtrName, false, "\t\t\t\t");

					postCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << arrayElementName << ");" << std::endl;
					break;
				}
				case ::ExportedClassTypeCategory::Resource:
				{
					std::string scriptName = "scriptObj";

					postCallActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, parameterInformation.flags, scriptName, "nativeObj[i]", "\t\t\t\t");
					postCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)" << std::endl;
					postCallActions << "\t\t\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
					postCallActions << "\t\t\t\telse" << std::endl;
					postCallActions << "\t\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
				}
				break;
				default:
					outs() << "Error: Type not supported as an AsyncOp return value. \n";
					break;
				}

				postCallActions << "\t\t\t}" << std::endl;
				postCallActions << "\t\t\tmonoObj = " << arrayName << ".GetInternal();" << std::endl;
			}

			postCallActions << "\t\t\treturn monoObj;\n";
			postCallActions << "\t\t};\n";
			postCallActions << "\n;";
		}
		else
			postCallActions << "\t\tauto convertCallback = nullptr;\n";

		if (returnValue)
			postCallActions << "\t\t" << parameterName << " = " << "ScriptAsyncOpBase::Create(" << argumentName << ", convertCallback, " << monoType << ");\n";
		else
			postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", " << "ScriptAsyncOpBase::Create(" << argumentName << ", convertCallback, " << monoType << "));\n";

		return argumentName;
	}

	if (!isArrayOrVector(parameterInformation.flags))
	{
		std::string argName;

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::Enum:
		case ::ExportedClassTypeCategory::Struct:
			if (returnValue)
			{
				argName = "tmp" + parameterName;

				if(isFlagsEnum(parameterInformation.flags))
					preCallActions << "\t\tFlags<" << parameterInformation.typeName << "> " << argName << ";" << std::endl;
				else
					preCallActions << "\t\t" << parameterInformation.typeName << " " << argName << ";" << std::endl;

				if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct)
				{
					if(isComplexStruct(parameterInformation.flags))
					{
						std::string scriptType = GetScriptInteropTypeName(parameterInformation.typeName);

						postCallActions << "\t\t" << GetStructInteropTypeName(parameterInformation.typeName) << " interop" << parameterName << ";\n";
						postCallActions << "\t\tinterop" << parameterName << " = " << scriptType << "::ToInterop(" << argName << ");\n";

						postCallActions << "\t\tMonoUtil::ValueCopy(" << parameterName << ", ";
						postCallActions << "&interop" << parameterName << ", ";
						postCallActions << scriptType << "::GetMetaData()->ScriptClass->GetInternalClassInternal());\n";
					}
					else
						postCallActions << "\t\t*" << parameterName << " = " << argName << ";" << std::endl;
				}
				else if(isFlagsEnum(parameterInformation.flags))
					postCallActions << "\t\t" << parameterName << " = (" << parameterInformation.typeName << ")(uint32_t)" << argName << ";" << std::endl;
				else
					postCallActions << "\t\t" << parameterName << " = " << argName << ";" << std::endl;
			}
			else if (isOutput(parameterInformation.flags))
			{
				if(parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct && isComplexStruct(parameterInformation.flags))
				{
					argName = "tmp" + parameterName;
					preCallActions << "\t\t" << parameterInformation.typeName << " " << argName << ";" << std::endl;

					std::string scriptType = GetScriptInteropTypeName(parameterInformation.typeName);

					postCallActions << "\t\t" << GetStructInteropTypeName(parameterInformation.typeName) << " interop" << parameterName << ";\n";
					postCallActions << "\t\tinterop" << parameterName << " = " << scriptType << "::ToInterop(" << argName << ");\n";

					postCallActions << "\t\tMonoUtil::ValueCopy(" << parameterName << ", ";
					postCallActions << "&interop" << parameterName << ", ";
					postCallActions << scriptType << "::GetMetaData()->ScriptClass->GetInternalClassInternal());\n";
				}
				else if (isFlagsEnum(parameterInformation.flags))
				{
					argName = "tmp" + parameterName;
					preCallActions << "\t\tFlags<" << parameterInformation.typeName << "> " << argName << ";" << std::endl;

					postCallActions << "\t\t*" << parameterName << " = (" << parameterInformation.typeName << ")(uint32_t)" << argName << ";" << std::endl;
				}
				else
					argName = parameterName;
			}
			else
			{
				if(parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct && isComplexStruct(parameterInformation.flags))
				{
					argName = "tmp" + parameterName;
					preCallActions << "\t\t" << parameterInformation.typeName << " " << argName << ";" << std::endl;

					std::string scriptType = GetScriptInteropTypeName(parameterInformation.typeName);
					preCallActions << "\t\t" << argName << " = " << scriptType << "::FromInterop(*" << parameterName << ");" << std::endl;
				}
				else
					argName = parameterName;
			}

			break;
		case ::ExportedClassTypeCategory::String:
		{
			argName = "tmp" + parameterName;
			preCallActions << "\t\tString " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = MonoUtil::StringToMono(" << argName << ");" << std::endl;
			else if (isOutput(parameterInformation.flags))
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ",  (MonoObject*)MonoUtil::StringToMono(" << argName << "));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::MonoToString(" << parameterName << ");" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::Path:
		{
			argName = "tmp" + parameterName;
			preCallActions << "\t\tPath " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = MonoUtil::StringToMono(" << argName << ".ToString());" << std::endl;
			else if (isOutput(parameterInformation.flags))
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ",  (MonoObject*)MonoUtil::StringToMono(" << argName << ".ToString()));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::MonoToString(" << parameterName << ");" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::WString:
		{
			argName = "tmp" + parameterName;
			preCallActions << "\t\tWString " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = MonoUtil::WstringToMono(" << argName << ");" << std::endl;
			else if (isOutput(parameterInformation.flags))
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", (MonoObject*)MonoUtil::WstringToMono(" << argName << "));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::MonoToWString(" << parameterName << ");" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::MonoObject:
		{
			argName = "tmp" + parameterName;
			
			if (returnValue)
			{
				preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
				postCallActions << "\t\t" << parameterName << " = " << argName << ";" << std::endl;
			}
			else if (isOutput(parameterInformation.flags))
			{
				preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", " << argName << ");" << std::endl;
			}
			else
			{
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
			}
		}
		break;
		case ::ExportedClassTypeCategory::GUIElement:
		{
			argName = "tmp" + parameterName;
			std::string tmpType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
			std::string scriptType = GetScriptInteropTypeName(parameterInformation.typeName);

			preCallActions << "\t\t" << tmpType << " " << argName << ";\n";
			if(returnValue || isOutput(parameterInformation.flags))
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
			else
			{
				std::string scriptName = "script" + parameterName;

				preCallActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, parameterName, parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << GenerateGetInternalCallLine(parameterInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}
		}
			break;
		case ::ExportedClassTypeCategory::Class:
		case ::ExportedClassTypeCategory::ReflectableClass:
		{
			argName = "tmp" + parameterName;
			std::string tmpType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
			std::string scriptType = GetScriptInteropTypeName(parameterInformation.typeName);

			preCallActions << "\t\t" << tmpType << " " << argName;
			if ((returnValue || isOutput(parameterInformation.flags)) && IsDereferenceRequired(parameterInformation.TypeInformation, parameterTypeMappingInformation))
				preCallActions << " = bs_shared_ptr_new<" << parameterInformation.typeName << ">()";

			preCallActions << ";\n";

			if (returnValue)
				postCallActions << GenerateNativeClassToScriptObject(parameterInformation.TypeInformation, parameterName, scriptType, argName);
			else if (isOutput(parameterInformation.flags))
				postCallActions << GenerateNativeClassToScriptObject(parameterInformation.TypeInformation, parameterName, scriptType, argName, true);
			else
			{
				std::string scriptName = "script" + parameterName;
				
				preCallActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, parameterName, parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << GenerateGetInternalCallLine(parameterInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}
		}
			break;
		default: // Some resource or game object type
		{
			argName = "tmp" + parameterName;
			std::string tmpType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);

			preCallActions << "\t\t" << tmpType << " " << argName << ";" << std::endl;

			std::string scriptName = "script" + parameterName;
			std::string scriptType = GetScriptInteropTypeName(parameterInformation.typeName, getPassAsResourceRef(parameterInformation.flags));

			if (returnValue)
			{
				postCallActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, parameterInformation.flags, scriptName, argName);
				postCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\t" << parameterName << " = " << scriptName << "->GetManagedInstance();" << std::endl;
				postCallActions << "\t\telse" << std::endl;
				postCallActions << "\t\t\t" << parameterName << " = nullptr;" << std::endl;
			}
			else if (isOutput(parameterInformation.flags))
			{
				postCallActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, parameterInformation.flags, scriptName, argName);
				postCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\tMonoUtil::ReferenceCopy(" << parameterName << ", " << scriptName << "->GetManagedInstance());" << std::endl;
				postCallActions << "\t\telse" << std::endl;
				postCallActions << "\t\t\t*" << parameterName << " = nullptr;" << std::endl;
			}
			else
			{
				preCallActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, parameterName, parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << GenerateGetInternalCallLine(parameterInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}
		}
		break;
		}

		return argName;
	}
	else
	{
		std::string entryType;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			entryType = parameterInformation.typeName;
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = GetScriptInteropTypeName(parameterInformation.typeName, getPassAsResourceRef(parameterInformation.flags));
			break;
		}

		std::string argType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
		std::string argName = "vec" + parameterName;

		const VariableTypeInformation& arrayElementTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		preCallActions << "\t\t" << argType << " " << argName;
		if (isArray(parameterInformation.flags))
			preCallActions << "[" << parameterInformation.arraySize << "]";
		preCallActions << ";\n";

		if (!isOutput(parameterInformation.flags) && !returnValue)
		{
			std::string arrayName = "array" + parameterName;

			preCallActions << "\t\tif(" << parameterName << " != nullptr)\n";
			preCallActions << "\t\t{\n";

			preCallActions << "\t\t\tScriptArray " << arrayName << "(" << parameterName << ");" << std::endl;

			if(isVector(parameterInformation.flags) || isSmallVector(parameterInformation.flags))
				preCallActions << "\t\t\t" << argName << ".resize(" << arrayName << ".Size());" << std::endl;

			preCallActions << "\t\t\tfor(int i = 0; i < (int)" << arrayName << ".Size(); i++)" << std::endl;
			preCallActions << "\t\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preCallActions << "\t\t\t\t" << argName << "[i] = " << arrayName << ".Get<" << entryType << ">(i);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preCallActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".Get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:

				preCallActions << "\t\t\t\t" << argName << "[i] = ";

				if (isComplexStruct(parameterInformation.flags))
				{
					preCallActions << entryType << "::FromInterop(";
					preCallActions << arrayName << ".Get<" << GetStructInteropTypeName(parameterInformation.typeName) << ">(i)";
					preCallActions << ")";
				}
				else
					preCallActions << arrayName << ".Get<" << parameterInformation.typeName << ">(i)";

				preCallActions << ";\n";

				break;
			default: // Some object type
			{
				std::string scriptName = "script" + parameterName;

				preCallActions << GenerateMonoToScriptObject("\t\t\t\t", entryType, scriptName, arrayName + ".Get<MonoObject*>(i)",parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preCallActions << "\t\t\t\t{\n";

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + parameterName;

				preCallActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					GenerateGetInternalCallLine(arrayElementTypeInformation, parameterTypeMappingInformation, scriptName) << ";\n";

				if(parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class || parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
				{
					if(isSrcPointer(parameterInformation.flags))
						preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".Get();\n";
					else if((isSrcReference(parameterInformation.flags) || isSrcValue(parameterInformation.flags)) && !isSrcSPtr(parameterInformation.flags))
					{
						preCallActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
						preCallActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
					}
					else
						preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
				}
				else
					preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";

				preCallActions << "\t\t\t\t}\n";
			}
			break;
			}

			preCallActions << "\t\t\t}" << std::endl;

			if (!isLast)
				preCallActions << std::endl;

			preCallActions << "\t\t}\n";
		}
		else
		{
			std::string arrayName = "array" + parameterName;

			postCallActions << "\t\tint arraySize" << parameterName << " = ";
			if (isVector(parameterInformation.flags) || isSmallVector(parameterInformation.flags))
				postCallActions << "(int)" << argName << ".size()";
			else
				postCallActions << parameterInformation.arraySize;
			postCallActions << ";\n";

			postCallActions << "\t\tScriptArray " << arrayName;
			postCallActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << parameterName << ");" << std::endl;
			postCallActions << "\t\tfor(int i = 0; i < arraySize" << parameterName << "; i++)" << std::endl;
			postCallActions << "\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				postCallActions << "\t\t\t" << arrayName << ".Set(i, " << argName << "[i]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				if(isFlagsEnum(parameterInformation.flags))
					postCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")(uint32_t)" << argName << "[i]);" << std::endl;
				else
					postCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")" << argName << "[i]);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				postCallActions << "\t\t\t" << arrayName << ".Set(i, ";

				if(isComplexStruct(parameterInformation.flags))
					postCallActions << entryType << "::ToInterop(";

				postCallActions << argName << "[i]";

				if (isComplexStruct(parameterInformation.flags))
					postCallActions << ")";

				postCallActions << ");\n";

				break;
			case ::ExportedClassTypeCategory::MonoObject:
				postCallActions << "\t\t\t" << arrayName << ".Set(i, " << argName << "[i]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Class:
			case ::ExportedClassTypeCategory::ReflectableClass:
			{
				std::string elemName = "arrayElem" + parameterName;

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + parameterName;

				postCallActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(IsDereferenceRequired(arrayElementTypeInformation, parameterTypeMappingInformation))
				{
					postCallActions << " = bs_shared_ptr_new<" << parameterInformation.typeName << ">();\n";

					if (isSrcPointer(parameterInformation.flags))
					{
						postCallActions << "\t\t\tif(" << argName << "[i])\n";
						postCallActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						postCallActions << "\t\t\t*" << elemPtrName << " = ";
					}

					postCallActions << argName << "[i];\n";
				}
				else
					postCallActions << " = " << argName << "[i];\n";

				postCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
				postCallActions << GenerateNativeClassToScriptObject(parameterInformation.TypeInformation, elemName, entryType, elemPtrName, false, "\t\t\t");

				postCallActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::GUIElement:
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
				break;
			default: // Some resource or game object type
			{
				std::string scriptName = "script" + parameterName;

				postCallActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, parameterInformation.flags, scriptName, argName + "[i]", "\t\t\t");
				postCallActions << "\t\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
				postCallActions << "\t\t\telse" << std::endl;
				postCallActions << "\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
			}
			break;
			}

			postCallActions << "\t\t}" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << arrayName << ".GetInternal();" << std::endl;
			else
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", (MonoObject*)" << arrayName << ".GetInternal());" << std::endl;
		}

		return argName;
	}
}

std::string generateFieldConvertBlock(const std::string& name, const VariableBase& fieldInformation, bool toInterop, std::stringstream& preActions)
{
	TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);

	if (getIsAsyncOp(fieldInformation.flags))
	{
		outs() << "Error: AsyncOp type not supported as a struct field. \n";
		return "";
	}

	if (!isArrayOrVector(fieldInformation.flags))
	{
		std::string arg;

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::Enum:
			arg = "value." + name;
			break;
		case ::ExportedClassTypeCategory::Struct:
			if(isComplexStruct(fieldInformation.flags))
			{
				std::string interopType = GetStructInteropTypeName(fieldInformation.typeName);
				std::string scriptType = GetScriptInteropTypeName(fieldInformation.typeName);

				arg = "tmp" + name;
				if(toInterop)
				{
					preActions << "\t\t" << interopType << " " << arg << ";" << std::endl;
					preActions << "\t\t" << arg << " = " << scriptType << "::ToInterop(value." << name << ");" << std::endl;
				}
				else
				{
					preActions << "\t\t" << fieldInformation.typeName << " " << arg << ";" << std::endl;
					preActions << "\t\t" << arg << " = " << scriptType << "::FromInterop(value." << name << ");" << std::endl;
				}
			}
			else
				arg = "value." + name;
			break;
		case ::ExportedClassTypeCategory::String:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::StringToMono(value." << name << ");" << std::endl;
			}
			else
			{
				preActions << "\t\tString " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::MonoToString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ::ExportedClassTypeCategory::WString:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::WstringToMono(value." << name << ");" << std::endl;
			}
			else
			{
				preActions << "\t\tWString " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::MonoToWString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ::ExportedClassTypeCategory::Path:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::StringToMono(value." << name << ".ToString());" << std::endl;
			}
			else
			{
				preActions << "\t\tPath " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::MonoToString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ::ExportedClassTypeCategory::MonoObject:
		{
			arg = "tmp" + name;

			preActions << "\t\tMonoObject* " << arg << ";" << std::endl;
			preActions << "\t\t" << arg << " = " << name << ";" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::GUIElement:
		{
			arg = "tmp" + name;
			std::string scriptType = GetScriptInteropTypeName(fieldInformation.typeName);

			if(!toInterop)
			{
				if(isSrcPointer(fieldInformation.flags))
				{
					std::string tmpType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
					preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;

					std::string scriptName = "script" + name;
					preActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, "value." + name, fieldInformation.TypeInformation, parameterTypeMappingInformation);
					preActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
					preActions << "\t\t\t" << arg << " = " << GenerateGetInternalCallLine(fieldInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
				}
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		case ::ExportedClassTypeCategory::Class:
		case ::ExportedClassTypeCategory::ReflectableClass:
		{
			arg = "tmp" + name;
			std::string scriptType = GetScriptInteropTypeName(fieldInformation.typeName);

			if(toInterop)
			{
				preActions << "\t\tMonoObject* " << arg << ";\n";

				// Need to copy by value
				if(isSrcValue(fieldInformation.flags) || isSrcPointer(fieldInformation.flags))
				{
					std::string tmpType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
					preActions << "\t\t" << tmpType << " " << arg << "copy;\n";

					// Note: Assuming a copy constructor exists
					if (isSrcPointer(fieldInformation.flags))
					{
						preActions << "\t\tif(value." << name << " != nullptr)\n";
						preActions << "\t\t\t" << arg << "copy = bs_shared_ptr_new<" << fieldInformation.typeName << ">(*value." << name << ");\n";
					}
					else
						preActions << "\t\t" << arg << "copy = bs_shared_ptr_new<" << fieldInformation.typeName << ">(value." << name << ");\n";

					preActions << GenerateNativeClassToScriptObject(fieldInformation.TypeInformation, arg, scriptType, arg + "copy");
				}
				else if(isSrcSPtr(fieldInformation.flags))
					preActions << GenerateNativeClassToScriptObject(fieldInformation.TypeInformation, arg, scriptType, "value." + name);
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
			else
			{
				std::string tmpType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;

				std::string scriptName = "script" + name;
				preActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, "value." + name, fieldInformation.TypeInformation, parameterTypeMappingInformation);
				preActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preActions << "\t\t\t" << arg << " = " << scriptName << "->GetInternal();" << std::endl;

				// Cast to the source type from SPtr
				if (isSrcValue(fieldInformation.flags))
				{
					preActions << "\t\tif(" << arg << " != nullptr)" << std::endl;
					arg = "*" + arg;
				}
				else if (isSrcPointer(fieldInformation.flags))
					arg = arg + ".get()";
				else if(!isSrcSPtr(fieldInformation.flags))
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		default: // Some resource or game object type
		{
			arg = "tmp" + name;
			std::string scriptType = GetScriptInteropTypeName(fieldInformation.typeName, getPassAsResourceRef(fieldInformation.flags));
			std::string scriptName = "script" + name;

			if(toInterop)
			{
				std::string argName;
				
				if(!getIsComponentOrActor(fieldInformation.flags))
					argName = "value." + name;
				else
					argName = "value." + name + ".GetComponent()";

				preActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, fieldInformation.flags, scriptName, argName);

				preActions << "\t\tMonoObject* " << arg << ";\n";
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << scriptName << "->GetManagedInstance();" << std::endl;
				preActions << "\t\telse\n";
				preActions << "\t\t\t" << arg << " = nullptr;\n";
			}
			else
			{
				std::string tmpType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;
				
				preActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, "value." + name, fieldInformation.TypeInformation, parameterTypeMappingInformation);
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << GenerateGetInternalCallLine(fieldInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}

			if(!isSrcGHandle(fieldInformation.flags) && !isSrcRHandle(fieldInformation.flags))
				outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
		}
		break;
		}

		return arg;
	}
	else
	{
		std::string entryType;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			entryType = fieldInformation.typeName;
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = GetScriptInteropTypeName(fieldInformation.typeName, getPassAsResourceRef(fieldInformation.flags));
			break;
		}

		std::string argType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
		std::string argName = "vec" + name;

		const VariableTypeInformation& arrayElementTypeInformation = fieldInformation.TypeInformation.AssertGetUnderlyingType();

		if (!toInterop)
		{
			std::string arrayName = "array" + name;
			preActions << "\t\t" << argType << " " << argName;
			if (isArray(fieldInformation.flags))
				preActions << "[" << fieldInformation.arraySize << "]";
			preActions << ";" << std::endl;

			preActions << "\t\tif(value." << name << " != nullptr)\n";
			preActions << "\t\t{\n";
			preActions << "\t\t\tScriptArray " << arrayName << "(value." << name << ");" << std::endl;

			if(isVector(fieldInformation.flags) || isSmallVector(fieldInformation.flags))
				preActions << "\t\t\t" << argName << ".resize(" << arrayName << ".Size());" << std::endl;

			preActions << "\t\t\tfor(int i = 0; i < (int)" << arrayName << ".Size(); i++)" << std::endl;
			preActions << "\t\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preActions << "\t\t\t\t" << argName << "[i] = " << arrayName << ".Get<" << entryType << ">(i);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				preActions << "\t\t\t\t" << argName << "[i] = ";

				if (isComplexStruct(fieldInformation.flags))
				{
					preActions << entryType << "::FromInterop(";
					preActions << arrayName << ".Get<" << GetStructInteropTypeName(fieldInformation.typeName) << ">(i)";
					preActions << ")";
				}
				else
					preActions << arrayName << ".Get<" << fieldInformation.typeName << ">(i)";

				preActions << ";\n";
				break;
			default: // Some object type
			{
				std::string scriptName = "script" + name;
				preActions << GenerateMonoToScriptObject("\t\t\t\t", entryType, scriptName, arrayName + ".Get<MonoObject*>(i)", fieldInformation.TypeInformation, parameterTypeMappingInformation);
				
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t{\n";

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					GenerateGetInternalCallLine(arrayElementTypeInformation, parameterTypeMappingInformation, scriptName) << ";\n";

				if(parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class || parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
				{
					if(isSrcPointer(fieldInformation.flags))
						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".get();\n";
					else if((isSrcReference(fieldInformation.flags) || isSrcValue(fieldInformation.flags)) && !isSrcSPtr(fieldInformation.flags))
					{
						preActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
						preActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
					}
					else
						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
				}
				else
					preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";

				preActions << "\t\t\t\t}\n";
			}
			break;
			}

			preActions << "\t\t\t}" << std::endl;
			preActions << "\t\t}\n";
		}
		else
		{
			preActions << "\t\tint arraySize" << name << " = ";
			if (isVector(fieldInformation.flags) || isSmallVector(fieldInformation.flags))
				preActions << "(int)value." << name << ".size()";
			else
				preActions << fieldInformation.arraySize;
			preActions << ";\n";

			preActions << "\t\tMonoArray* " << argName << ";" << std::endl;

			std::string arrayName = "array" + name;
			preActions << "\t\tScriptArray " << arrayName;
			preActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
			preActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
			preActions << "\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preActions << "\t\t\t" << arrayName << ".Set(i, value." << name << "[i]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")value." << name << "[i]);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				preActions << "\t\t\t" << arrayName << ".Set(i, ";

				if(isComplexStruct(fieldInformation.flags))
					preActions << entryType << "::ToInterop(";

				preActions << "value." << name << "[i]";

				if (isComplexStruct(fieldInformation.flags))
					preActions << ")";

				preActions << ");\n";
				break;
			case ::ExportedClassTypeCategory::MonoObject:
				preActions << "\t\t\t" << arrayName << ".Set(i, value." << name << "[i]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Class:
			case ::ExportedClassTypeCategory::ReflectableClass:
			{
				std::string elemName = "arrayElem" + name;

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(IsDereferenceRequired(arrayElementTypeInformation, parameterTypeMappingInformation))
				{
					preActions << " = bs_shared_ptr_new<" << fieldInformation.typeName << ">();\n";

					if (isSrcPointer(fieldInformation.flags))
					{
						preActions << "\t\t\tif(value." << name << "[i])\n";
						preActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						preActions << "\t\t\t*" << elemPtrName << " = ";
					}

					preActions << "value." << name << "[i];\n";
				}
				else
					preActions << " = value." << name << "[i];\n";

				preActions << "\t\t\tMonoObject* " << elemName << ";\n";
				preActions << GenerateNativeClassToScriptObject(fieldInformation.TypeInformation, elemName, entryType, elemPtrName, false, "\t\t\t");

				preActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
			}
			break;
			case ::ExportedClassTypeCategory::GUIElement:
				// Unsupported as output
				break;
			default: // Some resource or game object type
			{
				std::string scriptName = "script" + name;

				preActions << GenerateNativeHandleToScriptObject(parameterTypeMappingInformation.TypeCategory, fieldInformation.flags, scriptName, "value." + name + "[i]", "\t\t\t");
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
				preActions << "\t\t\telse\n";
				preActions << "\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
			}
			break;
			}

			preActions << "\t\t}" << std::endl;
			preActions << "\t\t" << argName << " = " << arrayName << ".GetInternal();" << std::endl;
		}

		return argName;
	}
}

std::string generateEventCallbackBodyBlockForParam(const std::string& name, const VariableBase& varTypeInfo, std::stringstream& preCallActions)
{
	TypeMappingInformation paramTypeInfo = GetNativeToScriptTypeMapping(varTypeInfo.TypeInformation);

	if (getIsAsyncOp(varTypeInfo.flags))
	{
		outs() << "Error: AsyncOp type not supported as an event callback parameter. \n";
		return "";
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string argName;

		switch (paramTypeInfo.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
			argName = name;
			break;
		case ::ExportedClassTypeCategory::Enum:
			if(isFlagsEnum(varTypeInfo.flags))
			{
				argName = "tmp" + name;
				preCallActions << "\t\t" << varTypeInfo.typeName << argName << ";" << std::endl;
				preCallActions << "\t\t" << argName << " = (" << varTypeInfo.typeName << ")(uint32_t)" << name << ";" << std::endl;
			}
			else
				argName = name;
			break;
		case ::ExportedClassTypeCategory::Struct:
			{
				argName = "tmp" + name;

				std::string scriptType = GetScriptInteropTypeName(varTypeInfo.typeName);
				preCallActions << "\t\tMonoObject* " << argName << ";\n";

				if(isComplexStruct(varTypeInfo.flags))
				{
					std::string interopName = "interop" + name;
					std::string interopType = GetStructInteropTypeName(varTypeInfo.typeName);
					
					preCallActions << "\t\t" << interopType << " " << interopName << ";" << std::endl;
					preCallActions << "\t\t" << interopName << " = " << scriptType << "::ToInterop(" << name << ");" << std::endl;
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << interopName << ");\n";
				}
				else
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << name << ");\n";
			}

			break;
		case ::ExportedClassTypeCategory::String:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::StringToMono(" << name << ");" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::WString:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::WstringToMono(" << name << ");" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::Path:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::StringToMono(" << name << ".ToString());" << std::endl;
		}
		break;
		case ::ExportedClassTypeCategory::MonoObject:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoObject* " << argName << " = " << name << ";\n";
		}
		break;
		case ::ExportedClassTypeCategory::Class:
		case ::ExportedClassTypeCategory::ReflectableClass:
		{
			argName = "tmp" + name;
			std::string scriptType = GetScriptInteropTypeName(varTypeInfo.typeName);

			preCallActions << "\t\tMonoObject* " << argName << ";\n";
			preCallActions << GenerateNativeClassToScriptObject(varTypeInfo.TypeInformation, argName, scriptType, name);
		}
			break;
		default: // Some resource or game object type
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;

			std::string scriptName = "script" + name;
			std::string scriptType = GetScriptInteropTypeName(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));

			preCallActions << GenerateNativeHandleToScriptObject(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, name);
			preCallActions << "\t\tif(" << scriptName << " != nullptr)\n";
			preCallActions << "\t\t\t" << argName << " = " << scriptName << "->GetManagedInstance();" << std::endl;
			preCallActions << "\t\telse\n";
			preCallActions << "\t\t\t" << argName << " = nullptr;\n";
		}
		break;
		}

		return argName;
	}
	else
	{
		std::string entryType;
		switch (paramTypeInfo.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = GetScriptInteropTypeName(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argName = "vec" + name;
		preCallActions << "\t\tMonoArray* " << argName << ";" << std::endl;

		preCallActions << "\t\tint arraySize" << name << " = ";
		if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
			preCallActions << "(int)value." << name << ".size()";
		else
			preCallActions << varTypeInfo.arraySize;
		preCallActions << ";\n";

		std::string arrayName = "array" + name;
		preCallActions << "\t\tScriptArray " << arrayName;
		preCallActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
		preCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
		preCallActions << "\t\t{" << std::endl;

		switch (paramTypeInfo.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << name << "[i]);" << std::endl;
			break;
		case ::ExportedClassTypeCategory::Enum:
		{
			std::string enumType;
			ParserUtility::MapBuiltinPrimitiveTypeToCppType(paramTypeInfo.EnumUnderlyingType, enumType);

			if(isFlagsEnum(varTypeInfo.flags))
				preCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")(uint32_t)" << name << "[i]);" << std::endl;
			else
				preCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")" << name << "[i]);" << std::endl;
			break;
		}
		case ::ExportedClassTypeCategory::Struct:
			preCallActions << "\t\t\t" << arrayName << ".Set(i, ";

			if (isComplexStruct(varTypeInfo.flags))
				preCallActions << entryType << "::ToInterop(";

			preCallActions << name << "[i]";

			if (isComplexStruct(varTypeInfo.flags))
				preCallActions << ")";

			preCallActions << ");\n";
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			preCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << name << "[i]);" << std::endl;
			break;
		case ::ExportedClassTypeCategory::Class:
		case ::ExportedClassTypeCategory::ReflectableClass:
		{
			std::string elemName = "arrayElem" + name;
			preCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
			preCallActions << GenerateNativeClassToScriptObject(varTypeInfo.TypeInformation, elemName, entryType, name + "[i]", false, "\t\t\t");
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
		}
		break;
		default: // Some resource or game object type
		{
			std::string scriptName = "script" + name;

			preCallActions << GenerateNativeHandleToScriptObject(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, name + "[i]", "\t\t\t");
			preCallActions << "\t\t\tif(" << scriptName << "[i] != nullptr)\n";
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
			preCallActions << "\t\t\telse\n";
			preCallActions << "\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
		}
		break;
		}

		preCallActions << "\t\t}" << std::endl;
		preCallActions << "\t\t" << argName << " = " << arrayName << ".GetInternal();" << std::endl;

		return argName;
	}
}

std::string generateCppMethodBody(const ClassInfo& classInfo, const MethodInfo& methodInfo, const std::string& sourceClassName,
	const std::string& interopClassName, const TypeMappingInformation& typeMappingInformation, bool isModule)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;

	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;
	bool isExternal = (methodInfo.flags & (int)MethodFlags::External) != 0;

	bool returnAsParameter = false;
	TypeMappingInformation returnTypeMappingInformation;
	if (!methodInfo.returnInfo.typeName.empty() && !isCtor)
	{
		returnTypeMappingInformation = GetNativeToScriptTypeMapping(methodInfo.returnInfo.TypeInformation);
		if (!CanBeReturned(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation))
			returnAsParameter = true;
		else
		{
			std::string returnType = GetCppInteropQualifiedTypeName(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation);
			postCallActions << "\t\t" << returnType << " __output;" << std::endl;

			std::string argName = GenerateMethodBodyBlockForArgument("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

			returnAssignment = argName + " = ";
			returnStmt = "\t\treturn __output;";
		}
	}

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const bool isLastArgument = (I + 1) == methodInfo.paramInfos.end();
		const std::string argumentName = GenerateMethodBodyBlockForArgument(I->Name, *I, isLastArgument, false, preCallActions, postCallActions);

		TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(I->TypeInformation);
		methodArgs << GetArgumentForInternalToNativeCall(methodInfo, argumentName, I->TypeInformation, parameterTypeMappingInformation);

		if (!isLastArgument)
			methodArgs << ", ";
	}

	if (returnAsParameter)
	{
		std::string argName = GenerateMethodBodyBlockForArgument("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	if (isCtor)
	{
		bool isValid = false;
		if (!isExternal)
		{
			if (isClassType(typeMappingInformation.TypeCategory))
			{
				output << "\t\tSPtr<" << sourceClassName << "> instance = bs_shared_ptr_new<" << sourceClassName << ">(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;

			if (isClassType(typeMappingInformation.TypeCategory))
			{
				output << "\t\tSPtr<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
			{
				output << "\t\tResourceHandle<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tScriptResourceManager::Instance().CreateBuiltinScriptResource(instance, managedInstance);" << std::endl;
				isValid = true;
			}
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
			{
				output << "\t\t" << sourceClassName << "* instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
		}

		if (!isValid)
			outs() << "Error: Cannot generate a constructor for \"" << sourceClassName << "\". Unsupported class type. \n";
	}
	else
	{
		std::stringstream methodCall;
		if (!isExternal)
		{
			if (isStatic)
				methodCall << sourceClassName << "::" << methodInfo.sourceName << "(" << methodArgs.str() << ")"; 
			else if(isModule)
				methodCall << sourceClassName << "::Instance()." << methodInfo.sourceName << "(" << methodArgs.str() << ")";
			else
			{
				VariableTypeInformation typeInformation;
				typeInformation.TypeName = sourceClassName;
				typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

				methodCall << GenerateGetInternalCallLine(typeInformation, typeMappingInformation, "thisPtr");
				methodCall << "->" << methodInfo.sourceName << "(" << methodArgs.str() << ")";
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;
			if (isStatic)
				methodCall << fullMethodName << "(" << methodArgs.str() << ")";
			else
			{
				VariableTypeInformation typeInformation;
				typeInformation.TypeName = sourceClassName;
				typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

				methodCall << fullMethodName << "(" << GenerateGetInternalCallLine(typeInformation, typeMappingInformation, "thisPtr");

				std::string methodArgsStr = methodArgs.str();
				if (!methodArgsStr.empty())
					methodCall << ", " << methodArgsStr;

				methodCall << ")";
			}
		}

		std::string call;
		if (!methodInfo.returnInfo.typeName.empty())
		{
			// Dereference input if needed
			if (isClassType(returnTypeMappingInformation.TypeCategory) && !isArrayOrVector(methodInfo.returnInfo.flags))
			{
				if ((isSrcPointer(methodInfo.returnInfo.flags) || isSrcReference(methodInfo.returnInfo.flags) || 
					isSrcValue(methodInfo.returnInfo.flags)) && !isSrcSPtr(methodInfo.returnInfo.flags))
					returnAssignment = "*" + returnAssignment;
			}

			call = GetReturnValueForNativeCall(methodCall.str(), methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation);
		}
		else
			call = methodCall.str();

		output << "\t\t" << returnAssignment << call << ";\n";
	}

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	if (!returnStmt.empty())
	{
		output << std::endl;
		output << returnStmt << std::endl;
	}

	output << "\t}" << std::endl;
	return output.str();
}

/**
 * Generates an interop method body that returns a value of a particular field.
 *
 * @param	classInfo				Class the field belongs to.
 * @param	fieldInfo				Information about the field.
 * @param	methodInfo				Method representing the propety getter for this field.
 * @param	typeMappingInformation	Information about the type mapped to script.
 * @param	isModule				True if generating a method for a Module signleton class.
 * @return							Contents of the getter interop method.
 */
std::string GenerateCppFieldGetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo, const TypeMappingInformation& typeMappingInformation, bool isModule)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;

	bool returnAsParameter = false;
	TypeMappingInformation returnTypeMappingInformation = GetNativeToScriptTypeMapping(methodInfo.returnInfo.TypeInformation);
	if (!CanBeReturned(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation))
		returnAsParameter = true;
	else
	{
		std::string returnType = GetCppInteropQualifiedTypeName(methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation);
		postCallActions << "\t\t" << returnType << " __output;" << std::endl;

		const std::string argumentName = GenerateMethodBodyBlockForArgument("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argumentName + " = ";
		returnStmt = "\t\treturn __output;";
	}

	if (returnAsParameter)
	{
		const std::string argumentName = GenerateMethodBodyBlockForArgument("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argumentName + " = ";
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.name << "::" << fieldInfo.Name; 
	else if(isModule)
		fieldAccess << classInfo.name << "::Instance()." << fieldInfo.Name;
	else
	{
		VariableTypeInformation typeInformation;
		typeInformation.TypeName = classInfo.name;
		typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

		fieldAccess << GenerateGetInternalCallLine(typeInformation, typeMappingInformation, "thisPtr");
		fieldAccess << "->" << fieldInfo.Name;
	}

	// Dereference input if needed
	if (isClassType(returnTypeMappingInformation.TypeCategory) && !methodInfo.returnInfo.TypeInformation.IsArrayOrVector())
	{
		// TODO - Need to investigate why is this done this way
		if ((isSrcPointer(methodInfo.returnInfo.flags) || isSrcReference(methodInfo.returnInfo.flags) || 
			isSrcValue(methodInfo.returnInfo.flags)) && !isSrcSPtr(methodInfo.returnInfo.flags))
			returnAssignment = "*" + returnAssignment;
	}

	const std::string access = GetReturnValueForNativeCall(fieldAccess.str(), methodInfo.returnInfo.TypeInformation, returnTypeMappingInformation);
	output << "\t\t" << returnAssignment << access << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << std::endl;
	output << returnStmt << std::endl;

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppFieldSetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo, const TypeMappingInformation& typeMappingInformation, bool isModule)
{
	std::stringstream preCallActions;
	std::stringstream argumentValue;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;

	const VariableInformation& parameterInformation = methodInfo.paramInfos[0];
	const std::string argumentName = GenerateMethodBodyBlockForArgument(parameterInformation.Name, parameterInformation, false, false, preCallActions, postCallActions);

	const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);
	argumentValue << GetArgumentForInternalToNativeCall(methodInfo, argumentName, parameterInformation.TypeInformation, parameterTypeMappingInformation);

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.name << "::" << fieldInfo.Name; 
	else if(isModule)
		fieldAccess << classInfo.name << "::Instance()." << fieldInfo.Name;
	else
	{
		VariableTypeInformation typeInformation;
		typeInformation.TypeName = classInfo.name;
		typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

		fieldAccess << GenerateGetInternalCallLine(typeInformation, typeMappingInformation, "thisPtr");
		fieldAccess << "->" << fieldInfo.Name;
	}

	output << "\t\t" << fieldAccess.str() << " = " << argumentValue.str() << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppEventCallbackBody(const MethodInfo& eventInfo, bool isModule)
{
	std::stringstream preCallActions;
	std::stringstream methodArgs;

	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	int idx = 0;
	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		const bool isLast = (I + 1) == eventInfo.paramInfos.end();
		const std::string argumentName = generateEventCallbackBodyBlockForParam(I->Name, *I, preCallActions);
		const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(I->TypeInformation);

		methodArgs << GetArgumentForInteropEventToThunkCall(eventInfo, argumentName, I->TypeInformation, parameterTypeMappingInformation);

		if (!isLast)
			methodArgs << ", ";

		idx++;
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	output << "\t\tMonoUtil::InvokeThunk(" << eventInfo.sourceName << "Thunk";

	if (!isStatic && !isModule)
		output << ", GetManagedInstance()";
	
	if (!eventInfo.paramInfos.empty())
		output << ", " << methodArgs.str();

	output << ");\n";

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppHeaderOutput(const ClassInfo& classInfo, const TypeMappingInformation& typeMappingInformation)
{
	bool inEditor = hasAPIBED (classInfo.api);
	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isModule = (classInfo.flags & (int)ClassFlags::IsModule) != 0;
	bool isRootBase = classInfo.baseClass.empty();

	bool hasStaticEvents = isModule && !classInfo.eventInfos.empty();
	if (!hasStaticEvents)
	{
		for (auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			if (isStatic)
			{
				hasStaticEvents = true;
				break;
			}
		}
	}

	std::string exportAttr;
	if (!inEditor)
		exportAttr = sFrameworkExportMacro;
	else
		exportAttr = sEditorExportMacro;

	std::string wrappedDataType = GetCppNativeQualifiedTypeName(classInfo.name, typeMappingInformation);
	std::string interopBaseClassName;

	std::stringstream output;
	output << GenerateApiCheckBegin(classInfo.api);

	// Generate a common base class if required
	// (GUIElements already have one by default)
	if(typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement)
	{
		if (isBase)
		{
			interopBaseClassName = GetScriptInteropTypeName(classInfo.name) + "Base";

			output << "\tclass " << exportAttr << " ";
			output << interopBaseClassName << " : public ";

			if (isRootBase)
			{
				if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class)
					output << "ScriptObjectBase";
				if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
					output << "ScriptReflectableBase";
				else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
					output << "ScriptComponentBase";
				else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
					output << "ScriptResourceBase";
			}
			else
			{
				std::string parentBaseClassName = GetScriptInteropTypeName(classInfo.baseClass) + "Base";
				output << parentBaseClassName;
			}

			output << std::endl;
			output << "\t{" << std::endl;
			output << "\tpublic:" << std::endl;
			output << "\t\t" << interopBaseClassName << "(MonoObject* instance);" << std::endl;
			output << "\t\tvirtual ~" << interopBaseClassName << "() {}" << std::endl;

			if(!isModule)
			{
				if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
				{
					output << std::endl;
					output << "\t\t" << wrappedDataType << " GetInternal() const;\n";
				}
				else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class)
				{
					output << std::endl;
					output << "\t\t" << wrappedDataType << " GetInternal() const { return mInternal; }" << std::endl;

					// Data member only present in the top-most base class
					if (isRootBase)
					{
						output << "\tprotected:" << std::endl;
						output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
					}
				}
			}

			output << "\t};" << std::endl;
			output << std::endl;
		}
		else if (!classInfo.baseClass.empty())
		{
			interopBaseClassName = GetScriptInteropTypeName(classInfo.baseClass) + "Base";
		}
	}

	// Generate main class
	output << "\tclass " << exportAttr << " ";;

	std::string interopClassName = GetScriptInteropTypeName(classInfo.name);
	output << interopClassName << " : public ";

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		output << "TScriptResource<" << interopClassName << ", " << classInfo.name;
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
		output << "TScriptComponent<" << interopClassName << ", " << classInfo.name;
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
		output << "TScriptGUIElement<" << interopClassName;
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
		output << "TScriptReflectable<" << interopClassName << ", " << classInfo.name;
	else // Class
		output << "ScriptObject<" << interopClassName;

	if (!interopBaseClassName.empty())
		output << ", " << interopBaseClassName;

	output << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if (!inEditor)
		output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, ENGINE_NS, \"" << typeMappingInformation.ScriptTypeName << "\")" << std::endl;
	else
		output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, EDITOR_NS, \"" << typeMappingInformation.ScriptTypeName << "\")" << std::endl;

	output << std::endl;

	// Constructor
	if (!isModule)
	{
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance, ";

		if (typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement)
			output << "const " << wrappedDataType << "& value";
		else
			output << wrappedDataType << " value";

		output << ");\n";
	}
	else
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;

	output << std::endl;

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class && !isModule)
	{
		// getInternal() method (handle types have getHandle() implemented by their base type)
		if (isBase || !classInfo.baseClass.empty())
			output << "\t\t" << wrappedDataType << " GetInternal() const;\n";
		else
			output << "\t\t" << wrappedDataType << " GetInternal() const { return mInternal; }" << std::endl;
	}

	if(isClassType(typeMappingInformation.TypeCategory) && !isModule)
	{
		// getManagedInstance() method (needed for events)
		if (!classInfo.eventInfos.empty())
			output << "\t\tMonoObject* GetManagedInstance() const;\n";

		// create() method
		output << "\t\tstatic MonoObject* Create(const " << wrappedDataType << "& value);" << std::endl;
		output << std::endl;
	}

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		// createInstance() method required by script resource manager
		output << "\t\tstatic MonoObject* CreateInstance();" << std::endl;
		output << std::endl;
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\t\tstatic void StartUp();" << std::endl;
		output << "\t\tstatic void ShutDown();" << std::endl;
		output << std::endl;
	}

	output << "\tprivate:" << std::endl;

	// Handle (if required)
	if (isClassType(typeMappingInformation.TypeCategory))
	{
		if (!classInfo.eventInfos.empty())
			output << "\t\tuint32_t mGCHandle = 0;\n\n";
	}

	// Event callback methods
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << GenerateApiCheckBegin(eventInfo.api);
		output << "\t\t" << GenerateEventCallbackSignature(eventInfo, "", isModule) << ";" << std::endl;
		output << GenerateApiCheckEnd(eventInfo.api);
	}

	if(!classInfo.eventInfos.empty())
		output << std::endl;

	// Data member
	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class && !isModule && classInfo.baseClass.empty() && !isBase)
	{
		output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
		output << std::endl;
	}

	// Event thunks
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << GenerateApiCheckBegin(eventInfo.api);
		output << GenerateEventThunkSignature(eventInfo, isModule);
		output << GenerateApiCheckEnd(eventInfo.api);
	}

	if(!classInfo.eventInfos.empty())
		output << std::endl;

	// Event handles
	for (auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
		if(!isCallback && (isStatic || isModule))
		{
			output << GenerateApiCheckBegin(eventInfo.api);
			output << "\t\tstatic HEvent " << eventInfo.sourceName << "Conn;" << std::endl;
			output << GenerateApiCheckEnd(eventInfo.api);
		}
	}

	if(hasStaticEvents)
		output << std::endl;

	// CLR hooks
	std::string interopClassThisPtrType;
	if (isBase)
	{
		if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
			interopClassThisPtrType = "ScriptGUIElementBaseTBase";
		else
			interopClassThisPtrType = interopBaseClassName;
	}
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		output << "\t\tstatic MonoObject* InternalGetRef(" << interopClassThisPtrType << "* thisPtr);\n\n";

	for (auto& methodInfo : classInfo.ctorInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << GenerateApiCheckBegin(methodInfo.api);
		output << "\t\tstatic " << GenerateInternalMethodSignature(methodInfo, interopClassThisPtrType, "", isModule) << ";" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.api);
	}

	for (auto& methodInfo : classInfo.methodInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << GenerateApiCheckBegin(methodInfo.api);
		output << "\t\tstatic " << GenerateInternalMethodSignature(methodInfo, interopClassThisPtrType, "", isModule) << ";" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.api);
	}

	output << "\t};" << std::endl;
	output << GenerateApiCheckEnd(classInfo.api);

	return output.str();
}

std::string generateCppSourceOutput(const ClassInfo& classInfo, const TypeMappingInformation& typeMappingInformation)
{
	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isModule = (classInfo.flags & (int)ClassFlags::IsModule) != 0;

	bool hasStaticEvents = isModule && !classInfo.eventInfos.empty();
	for(auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		if(isStatic)
		{
			hasStaticEvents = true;
			break;
		}
	}

	std::string interopClassName = GetScriptInteropTypeName(classInfo.name);
	std::string wrappedDataType = GetCppNativeQualifiedTypeName(classInfo.name, typeMappingInformation);

	std::string interopBaseClassName;

	if(typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement)
	{
		if (isBase)
			interopBaseClassName = GetScriptInteropTypeName(classInfo.name) + "Base";
		else if (!classInfo.baseClass.empty())
			interopBaseClassName = GetScriptInteropTypeName(classInfo.baseClass) + "Base";
	}

	std::stringstream output;
	output << GenerateApiCheckBegin(classInfo.api);

	if (isBase && typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement)
	{
		// Base class constructor
		output << "\t" << interopBaseClassName << "::" << interopBaseClassName << "(MonoObject* managedInstance)\n";
		output << "\t\t:";

		bool isRootBase = classInfo.baseClass.empty();
		if (isRootBase)
		{
			if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class)
				output << "ScriptObjectBase";
			if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
				output << "ScriptReflectableBase";
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
				output << "ScriptComponentBase";
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
				output << "ScriptResourceBase";
		}
		else
		{
			std::string parentBaseClassName = GetScriptInteropTypeName(classInfo.baseClass) + "Base";
			output << parentBaseClassName;
		}

		output << "(managedInstance)\n";
		output << "\t { }\n";
		output << "\n";

		// Base class getInternal() method
		if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
		{
			output << "\t" << wrappedDataType << " " << interopBaseClassName << "::" << "GetInternal() const\n";
			output << "\t{\n";
			output << "\t\treturn std::static_pointer_cast<" << classInfo.name << ">(mInternal);\n";
			output << "\t}\n";
		}
	}

	// Event thunks
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << GenerateApiCheckBegin(eventInfo.api);
		output << "\t" << interopClassName << "::" << eventInfo.sourceName << "ThunkDef " << interopClassName << "::" << eventInfo.sourceName << "Thunk; \n";
		output << GenerateApiCheckEnd(eventInfo.api);
	}

	if (!classInfo.eventInfos.empty())
		output << "\n";

	// Event handles
	bool hasEventHandles = false;
	for (auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
		if(!isCallback && (isStatic || isModule))
		{
			output << GenerateApiCheckBegin(eventInfo.api);
			output << "\tHEvent " << interopClassName << "::" << eventInfo.sourceName << "Conn;\n";
			output << GenerateApiCheckEnd(eventInfo.api);

			hasEventHandles = true;
		}
	}

	if (hasEventHandles)
		output << "\n";

	// Constructor
	if (!isModule)
	{
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance, ";

		if (typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement)
			output << "const " << wrappedDataType << "& value";
		else
			output << wrappedDataType << " value";

		output << ")\n";
	}
	else
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance)" << std::endl;

	output << "\t\t:";

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		output << "TScriptResource(managedInstance, value)";
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
		output << "TScriptComponent(managedInstance, value)";
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
		output << "TScriptGUIElement(managedInstance, value)";
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
	{
		if(!isModule)
			output << "TScriptReflectable(managedInstance, value)";
		else
			output << "TScriptReflectable(managedInstance, nullptr)";
	}
	else // Class
	{
		if(!isModule && !isBase && classInfo.baseClass.empty())
			output << "ScriptObject(managedInstance), mInternal(value)";
		else
			output << "ScriptObject(managedInstance)";
	}
	output << std::endl;
	output << "\t{" << std::endl;

	if (isClassType(typeMappingInformation.TypeCategory))
	{
		if (!classInfo.eventInfos.empty())
			output << "\t\tmGCHandle = MonoUtil::NewWeakGcHandle(managedInstance);\n";

		if (!isModule && (isBase || !classInfo.baseClass.empty()))
			output << "\t\tmInternal = value;\n";
	}

	// Register any non-static events
	if (!isModule)
	{
		for (auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if (!isStatic)
			{
				output << GenerateApiCheckBegin(eventInfo.api);

				if (!isCallback)
					output << "\t\tvalue->" << eventInfo.sourceName << ".Connect(";
				else
					output << "\t\tvalue->" << eventInfo.sourceName << " = ";

				output << "std::bind(&" << interopClassName << "::" << eventInfo.interopName << ", this";

				for (int i = 0; i < (int)eventInfo.paramInfos.size(); i++)
					output << ", std::placeholders::_" << (i + 1);

				if (!isCallback)
					output << ")";

				output << ");\n";
				output << GenerateApiCheckEnd(eventInfo.api);
			}
		}
	}

	output << "\t}" << std::endl;
	output << std::endl;

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class)
	{
		// getInternal method
		if (isBase || !classInfo.baseClass.empty())
		{
			output << "\t" << wrappedDataType << " " << interopClassName << "::GetInternal() const \n";
			output << "\t{\n";
			output << "\t\treturn std::static_pointer_cast<" << classInfo.name << ">(mInternal);\n";
			output << "\t}\n\n";
		}
	}

	if (isClassType(typeMappingInformation.TypeCategory) && !isModule)
	{
		// getManagedInstance() method (needed for events)
		if (!classInfo.eventInfos.empty())
		{
			output << "\tMonoObject* " << interopClassName << "::GetManagedInstance() const\n";
			output << "\t{\n";
			output << "\t\treturn MonoUtil::GetObjectFromGCHandle(mGCHandle);\n";
			output << "\t}\n\n";
		}
	}

	// CLR hook registration
	output << "\tvoid " << interopClassName << "::InitRuntimeData()" << std::endl;
	output << "\t{" << std::endl;

	// Internal_GetRef interop method
	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		output << "\t\tmetaData.ScriptClass->AddInternalCall(\"Internal_GetRef\", (void*)&" <<
			interopClassName << "::InternalGetRef);\n";
	}

	for (auto& methodInfo : classInfo.ctorInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << GenerateApiCheckBegin(methodInfo.api);
		output << "\t\tmetaData.ScriptClass->AddInternalCall(\"Internal_" << methodInfo.interopName << "\", (void*)&" <<
			interopClassName << "::Internal" << methodInfo.interopName << ");" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.api);
	}

	for (auto& methodInfo : classInfo.methodInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << GenerateApiCheckBegin(methodInfo.api);
		output << "\t\tmetaData.ScriptClass->AddInternalCall(\"Internal_" << methodInfo.interopName << "\", (void*)&" <<
			interopClassName << "::Internal" << methodInfo.interopName << ");" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.api);
	}

	output << std::endl;

	for(auto& eventInfo : classInfo.eventInfos)
	{
		output << GenerateApiCheckBegin(eventInfo.api);
		output << "\t\t" << eventInfo.sourceName << "Thunk = ";
		output << "(" << eventInfo.sourceName << "ThunkDef)metaData.ScriptClass->GetMethodExact(";
		output << "\"Internal_" << eventInfo.interopName << "\", \"";

		for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
		{
			const VariableInformation& paramInfo = *I;
			TypeMappingInformation paramaterTypeMappingInformation = GetNativeToScriptTypeMapping(paramInfo.TypeInformation);

			const std::string signatureTypeName = GetInteropThunkSignatureQualifiedTypeName(paramInfo.TypeInformation, paramaterTypeMappingInformation);

			output << signatureTypeName;

			if ((I + 1) != eventInfo.paramInfos.end())
				output << ",";
		}

		output << "\")->GetThunk();" << std::endl;
		output << GenerateApiCheckEnd(eventInfo.api);
	}

	output << "\t}" << std::endl;
	output << std::endl;

	// create() or createInstance() methods
	if ((isClassType(typeMappingInformation.TypeCategory) && !isModule) || typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		std::stringstream ctorSignature;
		std::stringstream ctorParamsInit;
		MethodInfo unusedCtor = findUnusedCtorSignature(classInfo);
		int numDummyParams = (int)unusedCtor.paramInfos.size();

		ctorParamsInit << "\t\tbool dummy = false;" << std::endl;
		ctorParamsInit << "\t\tvoid* ctorParams[" << numDummyParams << "] = { ";

		for (int i = 0; i < numDummyParams; i++)
		{
			ctorParamsInit << "&dummy";
			ctorSignature << unusedCtor.paramInfos[i].typeName;

			if ((i + 1) < numDummyParams)
			{
				ctorParamsInit << ", ";
				ctorSignature << ",";
			}
		}

		ctorParamsInit << " };" << std::endl;
		ctorParamsInit << std::endl;

		if (isClassType(typeMappingInformation.TypeCategory))
		{
			output << "\tMonoObject* " << interopClassName << "::Create(const " << wrappedDataType << "& value)" << std::endl;
			output << "\t{" << std::endl;
			output << "\t\tif(value == nullptr) return nullptr; " << std::endl;
			output << std::endl;

			output << ctorParamsInit.str();
			output << "\t\tMonoObject* managedInstance = metaData.ScriptClass->CreateInstance(\"" << ctorSignature.str() << "\", ctorParams);" << std::endl;
			output << "\t\tnew (bs_alloc<" << interopClassName << ">()) " << interopClassName << "(managedInstance, value);" << std::endl;
			output << "\t\treturn managedInstance;" << std::endl;

			output << "\t}" << std::endl;
		}
		else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		{
			output << "\t MonoObject*" << interopClassName << "::CreateInstance()" << std::endl;
			output << "\t{" << std::endl;

			output << ctorParamsInit.str();
			output << "\t\treturn metaData.ScriptClass->CreateInstance(\"" << ctorSignature.str() << "\", ctorParams);" << std::endl;

			output << "\t}" << std::endl;
		}
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\tvoid " << interopClassName << "::StartUp()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if (!isCallback)
			{
				if (isStatic)
				{
					output << "\t\t" << eventInfo.sourceName << "Conn = ";
					output << classInfo.name << "::" << eventInfo.sourceName << ".Connect(&" << interopClassName << "::" << eventInfo.interopName << ");" << std::endl;
				}
				else if (isModule)
				{
					output << "\t\t" << eventInfo.sourceName << "Conn = ";
					output << classInfo.name << "::Instance()." << eventInfo.sourceName << ".Connect(&" << interopClassName << "::" << eventInfo.interopName << ");" << std::endl;
				}
			}
			else
			{
				if (isStatic)
					output << classInfo.name << "::" << eventInfo.sourceName << " = &" << interopClassName << "::" << eventInfo.interopName << ";" << std::endl;
				else if (isModule)
					output << classInfo.name << "::Instance()." << eventInfo.sourceName << " = &" << interopClassName << "::" << eventInfo.interopName << ";" << std::endl;
			}
		}

		output << "\t}" << std::endl;

		output << "\tvoid " << interopClassName << "::ShutDown()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if(!isCallback && (isStatic || isModule))
				output << "\t\t" << eventInfo.sourceName << "Conn.Disconnect();" << std::endl;
		}

		output << "\t}" << std::endl;
		output << std::endl;
	}

	// Event callback method implementations
	for (auto I = classInfo.eventInfos.begin(); I != classInfo.eventInfos.end(); ++I)
	{
		const MethodInfo& eventInfo = *I;

		output << GenerateApiCheckBegin(eventInfo.api);
		output << "\t" << GenerateEventCallbackSignature(eventInfo, interopClassName, isModule) << std::endl;
		output << generateCppEventCallbackBody(eventInfo, isModule);
		output << GenerateApiCheckEnd(eventInfo.api);

		if ((I + 1) != classInfo.eventInfos.end())
			output << std::endl;
	}

	// CLR hook method implementations
	std::string interopClassThisPtrType;
	if (isBase)
	{
		if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
			interopClassThisPtrType = "ScriptGUIElementBaseTBase";
		else
			interopClassThisPtrType = interopBaseClassName;
	}
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		output << "\tMonoObject* " << interopClassName << "::InternalGetRef(" << interopClassThisPtrType << "* thisPtr)\n";
		output << "\t{\n";
		output << "\t\treturn thisPtr->GetRRef();\n";
		output << "\t}\n\n";
	}

	// Constructors
	for (auto I = classInfo.ctorInfos.begin(); I != classInfo.ctorInfos.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (isCSOnly(methodInfo.flags))
			continue;

		output << GenerateApiCheckBegin(methodInfo.api);
		output << "\t" << GenerateInternalMethodSignature(methodInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppMethodBody(classInfo, methodInfo, classInfo.name, interopClassName, typeMappingInformation, isModule);
		output << GenerateApiCheckEnd(methodInfo.api);

		if ((I + 1) != classInfo.methodInfos.end())
			output << std::endl;
	}

	// Methods
	for (auto I = classInfo.methodInfos.begin(); I != classInfo.methodInfos.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (isCSOnly(methodInfo.flags))
			continue;

		if ((methodInfo.flags & (int)MethodFlags::FieldWrapper) != 0)
			continue;

		output << GenerateApiCheckBegin(methodInfo.api);
		output << "\t" << GenerateInternalMethodSignature(methodInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppMethodBody(classInfo, methodInfo, classInfo.name, interopClassName, typeMappingInformation, isModule);
		output << GenerateApiCheckEnd(methodInfo.api);

		if ((I + 1) != classInfo.methodInfos.end())
			output << std::endl;
	}

	// Field wrapper methods
	for(auto I = classInfo.fieldInfos.begin(); I != classInfo.fieldInfos.end(); ++I)
	{
		const MethodInfo* setterInfo = nullptr;
		const MethodInfo* getterInfo = nullptr;

		std::string getterName = "Get" + I->Name;
		std::string setterName = "Set" + I->Name;
		for(auto& entry : classInfo.methodInfos)
		{
			if ((entry.flags & (int)MethodFlags::FieldWrapper) == 0)
				continue;

			if (entry.sourceName == getterName)
				getterInfo = &entry;
			else if (entry.sourceName == setterName)
				setterInfo = &entry;

			if (getterInfo != nullptr && setterInfo != nullptr)
				break;
		}

		assert(getterInfo && setterInfo);

		output << GenerateApiCheckBegin(getterInfo->api);
		output << "\t" << GenerateInternalMethodSignature(*getterInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << GenerateCppFieldGetterBody(classInfo, *I, *getterInfo, typeMappingInformation, isModule);
		output << GenerateApiCheckEnd(getterInfo->api);
		
		output << std::endl;

		output << GenerateApiCheckBegin(setterInfo->api);
		output << "\t" << GenerateInternalMethodSignature(*setterInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppFieldSetterBody(classInfo, *I, *setterInfo, typeMappingInformation, isModule);
		output << GenerateApiCheckEnd(setterInfo->api);
			
		if ((I + 1) != classInfo.fieldInfos.end())
			output << std::endl;
	}

	output << GenerateApiCheckEnd(classInfo.api);

	return output.str();
}

std::string generateCppStructHeader(const StructInfo& structInfo)
{
	TypeMappingInformation typeInfo = GetNativeToScriptTypeMapping(structInfo.name);

	std::stringstream output;
	output << GenerateApiCheckBegin(structInfo.api);

	if(structInfo.requiresInterop)
	{
		output << "\tstruct " << structInfo.interopName << "\n";
		output << "\t{\n";

		for(auto& fieldInfo : structInfo.fields)
		{
			TypeMappingInformation fieldTypeMappingInformation = GetNativeToScriptTypeMapping(fieldInfo.TypeInformation);

			output << "\t\t";
			output << GetCppInteropQualifiedTypeName(fieldInfo.TypeInformation, fieldTypeMappingInformation, true);
			output << " " << fieldInfo.Name << ";\n";
		}

		output << "\t};\n\n";
	}

	output << "\tclass ";

	bool inEditor = hasAPIBED (structInfo.api);
	if (!inEditor)
		output << sFrameworkExportMacro << " ";
	else
		output << sEditorExportMacro << " ";

	std::string interopClassName = GetScriptInteropTypeName(structInfo.name);
	output << interopClassName << " : public " << "ScriptObject<" << interopClassName << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if (!inEditor)
		output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, ENGINE_NS, \"" << typeInfo.ScriptTypeName << "\")" << std::endl;
	else
		output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, EDITOR_NS, \"" << typeInfo.ScriptTypeName << "\")" << std::endl;

	output << std::endl;

	output << "\t\tstatic MonoObject* Box(const " << structInfo.interopName << "& value);" << std::endl;
	output << "\t\tstatic " << structInfo.interopName << " Unbox(MonoObject* value);" << std::endl;

	if(structInfo.requiresInterop)
	{
		output << "\t\tstatic " << structInfo.name << " FromInterop(const " << structInfo.interopName << "& value);\n";
		output << "\t\tstatic " << structInfo.interopName << " ToInterop(const " << structInfo.name << "& value);\n";
	}

	output << std::endl;
	output << "\tprivate:" << std::endl;

	// Constructor
	output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;
	output << std::endl;

	output << "\t};" << std::endl;
	output << GenerateApiCheckEnd(structInfo.api);

	return output.str();
}

std::string generateCppStructSource(const StructInfo& structInfo)
{
	TypeMappingInformation typeInfo = GetNativeToScriptTypeMapping(structInfo.name);
	std::string interopClassName = GetScriptInteropTypeName(structInfo.name);

	std::stringstream output;
	output << GenerateApiCheckBegin(structInfo.api);

	// Constructor
	output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance)" << std::endl;
	output << "\t\t:ScriptObject(managedInstance)" << std::endl;
	output << "\t{ }" << std::endl;
	output << std::endl;

	// Empty initRuntimeData
	output << "\tvoid " << interopClassName << "::InitRuntimeData()" << std::endl;
	output << "\t{ }" << std::endl;
	output << std::endl;

	// Box
	output << "\tMonoObject*" << interopClassName << "::Box(const " << structInfo.interopName << "& value)" << std::endl;
	output << "\t{" << std::endl;
	output << "\t\treturn MonoUtil::Box(metaData.ScriptClass->GetInternalClassInternal(), (void*)&value);" << std::endl;
	output << "\t}" << std::endl;
	output << std::endl;

	// Unbox
	output << "\t" << structInfo.interopName << " " << interopClassName << "::Unbox(MonoObject* value)" << std::endl;
	output << "\t{" << std::endl;
	output << "\t\treturn *(" << structInfo.interopName << "*)MonoUtil::Unbox(value);" << std::endl;
	output << "\t}" << std::endl;
	output << std::endl;

	if(structInfo.requiresInterop)
	{
		// Convert from interop
		output << "\t" << structInfo.name << " " << interopClassName << "::FromInterop(const " << structInfo.interopName << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.name << " output;\n";
		for (auto& fieldInfo : structInfo.fields)
		{
			// Arrays can be assigned, so copy them entry by entry
			if(isArray(fieldInfo.flags))
			{
				std::string argName = generateFieldConvertBlock(fieldInfo.Name, fieldInfo, false, output);

				output << "\t\tauto tmp" << fieldInfo.Name << " = " << argName << ";\n";
				output << "\t\tfor(int i = 0; i < " << fieldInfo.arraySize << "; ++i)\n";
				output << "\t\t\toutput." << fieldInfo.Name << "[i] = tmp" << fieldInfo.Name << "[i];\n";
			}
			else
			{
				std::string argName = generateFieldConvertBlock(fieldInfo.Name, fieldInfo, false, output);

				output << "\t\toutput." << fieldInfo.Name << " = " << argName << ";\n";
			}
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";

		// Convert to interop
		output << "\t" << structInfo.interopName << " " << interopClassName << "::ToInterop(const " << structInfo.name << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.interopName << " output;\n";
		for(auto& fieldInfo : structInfo.fields)
		{
			std::string argName = generateFieldConvertBlock(fieldInfo.Name, fieldInfo, true, output);

			output << "\t\toutput." << fieldInfo.Name << " = " << argName << ";\n";
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";
	}

	output << GenerateApiCheckEnd(structInfo.api);
	return output.str();
}

void generateLookupFile(const std::string& tableName, ExportedClassTypeCategory type, bool editor,
						const std::string& engineOutputFolder, const std::string& editorOutputFolder)
{
	StringRef cppOutputFolder = editor ? editorOutputFolder : engineOutputFolder;

	std::stringstream body;
	std::stringstream includes;
	for (auto& fileInfo : outputFileInfos)
	{
		auto& classInfos = fileInfo.second.classInfos;
		if (classInfos.empty())
			continue;

		if(fileInfo.second.inEditor != editor)
			continue;

		bool hasType = false;
		for (auto& classInfo : classInfos)
		{
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];
			if (typeInfo.TypeCategory != type)
				continue;

			includes << GenerateApiCheckBegin(classInfo.api);
			includes << "#include \"" << getRelativeTo(typeInfo.NativeFile, cppOutputFolder) << "\"" << std::endl;
			includes << GenerateApiCheckEnd(classInfo.api);

			std::string interopClassName = GetScriptInteropTypeName(classInfo.name);
			body << GenerateApiCheckBegin(classInfo.api);
			body << "\t\tADD_ENTRY(" << classInfo.name << ", " << interopClassName << ")" << std::endl;
			body << GenerateApiCheckEnd(classInfo.api);

			hasType = true;
		}

		if(hasType)
			includes << "#include \"BsScript" + fileInfo.first + ".generated.h\"" << std::endl;
	}

	std::string prefix = editor ? "Editor" : "";
	std::ofstream output = createFile("Bs" + prefix + tableName + "Lookup.generated.h", cppOutputFolder);

	// License/copyright header
	output << generateFileHeader(editor);

	output << "#pragma once" << std::endl;
	output << std::endl;

	output << "#include \"Serialization/Bs" << tableName << "Lookup.h\"" << std::endl;
	output << "#include \"Reflection/BsRTTIType.h\"" << std::endl;
	output << includes.str();

	output << std::endl;

	output << "namespace " << (editor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
	output << "{" << std::endl;
	output << "\tLOOKUP_BEGIN(" << prefix << tableName << ")" << std::endl;

	output << body.str();

	output << "\tLOOKUP_END" << std::endl;
	output << "}" << std::endl;

	output << "#undef LOOKUP_BEGIN" << std::endl;
	output << "#undef ADD_ENTRY" << std::endl;
	output << "#undef LOOKUP_END" << std::endl;

	output.close();
}

void GenerateCpp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditor)
{
	cleanAndPrepareFolder(engineOutputFolder);

	if(generateEditor)
	{
		cleanAndPrepareFolder(editorOutputFolder);
	}

	// Generate H
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !generateEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			ClassInfo& classInfo = *I;
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

			body << generateCppHeaderOutput(classInfo, typeInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			StructInfo& structInfo = *I;
			body << generateCppStructHeader(structInfo);

			if ((I + 1) != structInfos.end())
				body << std::endl;
		}

		StringRef cppOutputFolder = fileInfo.second.inEditor ? editorOutputFolder : engineOutputFolder;
		std::ofstream output = createFile("BsScript" + fileInfo.first + ".generated.h", cppOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		output << "#pragma once" << std::endl;
		output << std::endl;

		// Output includes
		for (auto& include : fileInfo.second.referencedHeaderIncludes)
			output << "#include \"" << getRelativeTo(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		// Output forward declarations
		for (auto& decl : fileInfo.second.forwardDeclarations)
		{
			for (auto& nsEntry : decl.ns)
				output << "namespace " << nsEntry << " { ";
			
			if (decl.templParams.size() > 0)
			{
				output << "template<";

				for (int i = 0; i < (int)decl.templParams.size(); ++i)
				{
					if (i != 0)
						output << ", ";

					output << decl.templParams[i].type << " T" << std::to_string(i);
				}

				output << "> ";
			}

			if (decl.isStruct)
				output << "struct " << decl.name << ";";
			else
				output << "class " << decl.name << ";";

			for (auto& nsEntry : decl.ns)
				output << " }";
			
			output << "\n";
		}

		output << "namespace " << (fileInfo.second.inEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate CPP
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !generateEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			ClassInfo& classInfo = *I;
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

			body << generateCppSourceOutput(classInfo, typeInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			body << generateCppStructSource(*I);

			if ((I + 1) != structInfos.end())
				body << std::endl;
		}

		StringRef cppOutputFolder = fileInfo.second.inEditor ? editorOutputFolder : engineOutputFolder;
		std::ofstream output = createFile("BsScript" + fileInfo.first + ".generated.cpp", cppOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		// Output includes
		for (auto& include : fileInfo.second.referencedSourceIncludes)
			output << "#include \"" << getRelativeTo(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		output << "namespace " << (fileInfo.second.inEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate builtin component lookup file
	generateLookupFile("BuiltinComponent", ::ExportedClassTypeCategory::Component, false, engineOutputFolder.str(), editorOutputFolder.str());

	// Generate C++ reflectable type lookup files
	generateLookupFile("BuiltinReflectableTypes", ::ExportedClassTypeCategory::ReflectableClass, false, engineOutputFolder.str(), editorOutputFolder.str());
	generateLookupFile("BuiltinReflectableTypes", ::ExportedClassTypeCategory::ReflectableClass, true, engineOutputFolder.str(), editorOutputFolder.str());
}
