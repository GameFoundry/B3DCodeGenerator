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
	// TODO - All uses of this can be removed and replaced with a SharedPointer check

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

/** Generates a MonoUtil::String/WstringToMono method call, appropriate to the provided type. Only valid for path and string types. */
static std::string GenerateStringToMonoCall(ExportedClassTypeCategory exportedClassTypeCategory, const std::string& argument)
{
	switch (exportedClassTypeCategory)
	{
		case ExportedClassTypeCategory::Path:
			return "MonoUtil::StringToMono(" + argument + ".ToString())";
		case ExportedClassTypeCategory::String:
			return "MonoUtil::StringToMono(" + argument + ")";
		case ExportedClassTypeCategory::WString:
			return "MonoUtil::WstringToMono(" + argument + ")";
	default:
		assert(false && "Invalid type for this method.");
		return argument;
	}
}

/** Generates a MonoUtil::String/WstringToMono method call, appropriate to the provided type. Only valid for path and string types. */
static std::string GenerateMonoToStringCall(ExportedClassTypeCategory exportedClassTypeCategory, const std::string& argument)
{
	switch (exportedClassTypeCategory)
	{
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::Path:
			return "MonoUtil::MonoToString(" + argument + ")";
		case ExportedClassTypeCategory::WString:
			return "MonoUtil::MonoToWstring(" + argument + ")";
	default:
		assert(false && "Invalid type for this method.");
		return argument;
	}
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
 * Converts a native class type argument into MonoObject. This should be only called on types that are exported to scripting a ExportedClassTypeCategory::Class or ExportedClassTypeCategory::ReflectableClass.
 *
 * @param typeInformation			Information about the native type to convert.
 * @param outputVariableName		Name of the variable to store the result in.
 * @param scriptType				Interop script type we're doing the conversion for.
 * @param inputVariableName			Name of the variable that's being converted.
 * @param performReferenceCopy		If true, reference copy operation will be performed when assigning the value to output. Required if writing the output as an output parameter to an internal method.
 * @param indent					Optional indent to apply to the generated code.
 * @return							Code that converts a native object to a MonoObject.
 */
std::string GenerateNativeClassToMonoObject(const VariableTypeInformation& typeInformation, const std::string& outputVariableName, 
	const std::string& scriptType, const std::string& inputVariableName, bool performReferenceCopy = false, const std::string& indent = "\t\t")
{
	std::stringstream output;

	// TODO - Need to modify this part if we wish to have persistence with managed objects
	auto fnGenerateCreateLine = [&output, &outputVariableName, performReferenceCopy](const std::string& scriptType, const std::string& inputVariableName, const std::string& indent)
	{
		if (performReferenceCopy)
			output << indent << "MonoUtil::ReferenceCopy(" << outputVariableName << ", " << scriptType << "::Create(" << inputVariableName << "));\n";
		else
			output << indent << outputVariableName << " = " << scriptType << "::Create(" << inputVariableName << ");\n";
	};

	if(typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass))
	{
		std::vector<std::string> derivedClasses;
		getDerivedClasses(typeInformation.GetLastWrappedOrSelfTypeName(), derivedClasses);

		if(!derivedClasses.empty())
		{
			output << indent << "if(" << inputVariableName << ")\n";
			output << indent << "{\n";

			output << indent << "\tif(rtti_is_of_type<" << derivedClasses[0] << ">(" << inputVariableName << "))\n";
			fnGenerateCreateLine(GetScriptInteropTypeName(derivedClasses[0]), "std::static_pointer_cast<" + derivedClasses[0] + ">(" + inputVariableName + ")", indent + "\t\t");

			for(uint32_t i = 1; i < (uint32_t)derivedClasses.size(); i++)
			{
				output << indent << "\telse if(rtti_is_of_type<" << derivedClasses[i] << ">(" << inputVariableName << "))\n";
				fnGenerateCreateLine(GetScriptInteropTypeName(derivedClasses[i]), "std::static_pointer_cast<" + derivedClasses[i] + ">(" + inputVariableName + ")", indent + "\t\t");
			}

			output << indent << "\telse\n";
			fnGenerateCreateLine(scriptType, inputVariableName, indent + "\t\t");


			output << indent << "}\n";
			output << indent << "else\n";
			fnGenerateCreateLine(scriptType, inputVariableName, indent + "\t");

			return output.str();
		}
	}
	else
		fnGenerateCreateLine(scriptType, inputVariableName, indent);

	return output.str();
}

/**
 * Converts a native handle type argument into MonoObject. This should be only called on game or resource object types.
 *
 * @param typeInformation			Information about the native type to convert.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @param inputVariableName			Name of the variable that's being converted.
 * @param arrayIndexVariable		Optional variable that specifies an array index to lookup in the @p inputVariableName. If non-empty, it's assumed both input and output are arrays.
 * @param scriptVariableName		Name of the temporary script type to store the script interop object in.
 * @param outputVariableName		Name of the variable to store the result in.
 * @param isOutputParameter			True if the output variable is an output parameter to scripting.
 * @param indent					Optional indent to apply to the generated code.
 * @return							Code that converts a native object to a MonoObject.
 */
std::string GenerateNativeHandleToMonoObject(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& inputVariableName, const std::string& arrayIndexVariable, const std::string& scriptVariableName, 
	const std::string& outputVariableName, bool isOutputParameter, const std::string& indent = "\t\t")
{
	// NOTE: scriptVariableName can be automatically deduced from output variable name, but I'm avoding doing that right now to prevent changes to generated code
	//const std::string scriptVariableName = "script" + outputVariableName;
	std::stringstream output;

	const std::string inputVariableAccess = arrayIndexVariable.empty() ? inputVariableName : inputVariableName + "[" + arrayIndexVariable + "]";

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		if(typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
		{
			output << indent << "ScriptRRefBase* " << scriptVariableName << ";\n";
			output << indent << scriptVariableName << " = ScriptResourceManager::Instance().GetScriptRRef(" << inputVariableAccess << ");\n";
		}
		else
		{
			output << indent << "ScriptResourceBase* " << scriptVariableName << ";\n";
			output << indent << scriptVariableName << " = ScriptResourceManager::Instance().GetScriptResource(" << inputVariableAccess << ", true);\n";
		}
	}
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
	{
		output << indent << "ScriptComponentBase* " << scriptVariableName << " = nullptr;\n";
		output << indent << "if(" << inputVariableAccess << ")\n";
		output << indent << "\t" << scriptVariableName << " = ScriptGameObjectManager::Instance().GetBuiltinScriptComponent(" << "static_object_cast<Component>(" << inputVariableAccess << "));\n";
	}
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::SceneObject)
	{
		output << indent << "ScriptSceneObject* " << scriptVariableName << " = nullptr;\n";
		output << indent << "if(" << inputVariableAccess << ")\n";
		output << indent << scriptVariableName << " = ScriptGameObjectManager::Instance().GetOrCreateScriptSceneObject(" << inputVariableAccess << ");\n";
	}
	else
		assert(false && "Unsupported type category");

	output << indent << "if(" << scriptVariableName << " != nullptr)\n";

	if(arrayIndexVariable.empty())
	{
		if(isOutputParameter)
		{
			output << indent << "\tMonoUtil::ReferenceCopy(" << outputVariableName << ", " << scriptVariableName << "->GetManagedInstance());\n";
			output << indent << "else\n";
			output << indent << "\t*" << outputVariableName << " = nullptr;\n";
		}
		else
		{
			output << indent << "\t" << outputVariableName << " = " << scriptVariableName << "->GetManagedInstance();\n";
			output << indent << "else\n";
			output << indent << "\t" << outputVariableName << " = nullptr;\n";
		}
	}
	else
	{
		output << indent << "\t" << outputVariableName << ".Set(" << arrayIndexVariable << ", " << scriptVariableName << "->GetManagedInstance());\n";
		output << indent << "else\n";
		output << indent << "\t" << outputVariableName << ".Set(" << arrayIndexVariable << ", nullptr);\n";
	}

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

	// Handle AsyncOp types
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
					postCallActions << GenerateNativeClassToMonoObject(asyncOpUnderlyingTypeInformation, "monoObj", scriptType, "nativeObj", false, "\t\t\t");
				else // Resource
				{
					postCallActions << GenerateNativeHandleToMonoObject(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation, "nativeObj", "", "scriptObj", "monoObj", false, "\t\t\t");
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
				case ExportedClassTypeCategory::ReflectableClass:
				case ExportedClassTypeCategory::Class:
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
					postCallActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, arrayElementName, scriptType, arrayElementPtrName, false, "\t\t\t\t");

					postCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << arrayElementName << ");" << std::endl;
					break;
				}
				case ExportedClassTypeCategory::Resource:
				{
					postCallActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, "nativeObj", "i", "scriptObj", arrayName, false, "\t\t\t\t");
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

	// Converts a native struct into an interop struct
	auto fnGenerateConvertToInteropStructPostCallActions = [&postCallActions](const std::string& inputVariableName, const std::string& outputVariableName, const std::string& inputType)
	{
		const std::string scriptType = GetScriptInteropTypeName(inputType);

		postCallActions << "\t\t" << GetStructInteropTypeName(inputType) << " interop" << outputVariableName << ";\n";
		postCallActions << "\t\tinterop" << outputVariableName << " = " << scriptType << "::ToInterop(" << inputVariableName << ");\n";

		postCallActions << "\t\tMonoUtil::ValueCopy(" << outputVariableName << ", ";
		postCallActions << "&interop" << outputVariableName << ", ";
		postCallActions << scriptType << "::GetMetaData()->ScriptClass->GetInternalClassInternal());\n";
	};

	const std::string parameterTypeName = parameterInformation.TypeInformation.GetLastWrappedOrSelfTypeName();
	const bool isOutputParameter = parameterInformation.TypeInformation.IsOutputParameter();

	// Handle non-array types
	if (!parameterInformation.TypeInformation.IsArrayOrVector())
	{
		const bool isFlags = parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum && parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags;
		const bool isPlainType =
			parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Primitive ||
			parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum && parameterInformation.TypeInformation.TypeCategory != VariableTypeCategory::Flags ||
			(parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && !parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed));
		const bool isClassType = parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass;

		// Primitive and non-complex structs can be passed as-is, except for return values, in which case we need to box them.
		// Flags need to be converted to their underlying enum type if they are an output parameter or return value.
		// All other types need conversion to the corresponding Mono type.
		const bool isTemporaryRequired = returnValue || (isOutputParameter && !isPlainType) || (!isPlainType && !isFlags);

		// Temporary is needed for any type that cannot be passed directly between native and interop
		std::string argumentName;
		if (isTemporaryRequired)
		{
			argumentName = "tmp" + parameterName;

			const std::string fullTypeName = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
			preCallActions << "\t\t" << fullTypeName << " " << argumentName;

			if (isClassType && (returnValue || isOutputParameter) && IsDereferenceRequired(parameterInformation.TypeInformation, parameterTypeMappingInformation))
				preCallActions << " = bs_shared_ptr_new<" << parameterTypeName << ">()"; // We'll be copying by value rather than just assigning the pointer, so initialize the destination

			preCallActions << ";\n";
		}

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
		case ExportedClassTypeCategory::Enum:
		case ExportedClassTypeCategory::Struct:
		{
			if (returnValue)
			{
				if (parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
				{
					if (parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
						fnGenerateConvertToInteropStructPostCallActions(argumentName, parameterName, parameterTypeName);
					else
						postCallActions << "\t\t*" << parameterName << " = " << argumentName << ";" << std::endl;
				}
				else if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
					postCallActions << "\t\t" << parameterName << " = (" << parameterTypeName << ")(uint32_t)" << argumentName << ";" << std::endl;
				else
					postCallActions << "\t\t" << parameterName << " = " << argumentName << ";" << std::endl;
			}
			else if (isOutputParameter)
			{
				if (parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					fnGenerateConvertToInteropStructPostCallActions(argumentName, parameterName, parameterTypeName);
				}
				else if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
				{
					postCallActions << "\t\t*" << parameterName << " = (" << parameterTypeName << ")(uint32_t)" << argumentName << ";" << std::endl;
				}
				else
					argumentName = parameterName;
			}
			else
			{
				if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct && parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					std::string scriptType = GetScriptInteropTypeName(parameterTypeName);
					preCallActions << "\t\t" << argumentName << " = " << scriptType << "::FromInterop(*" << parameterName << ");" << std::endl;
				}
				else
					argumentName = parameterName;
			}
		}

			break;
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::Path:
		{
			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, argumentName) << ";\n";
			else if (isOutputParameter)
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ",  (MonoObject*)" << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, argumentName) << ");\n";
			else
				preCallActions << "\t\t" << argumentName << " = " << GenerateMonoToStringCall(parameterTypeMappingInformation.TypeCategory, parameterName) << ";\n";
		}
		break;
		case ExportedClassTypeCategory::MonoObject:
		{
			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << argumentName << ";" << std::endl;
			else if (isOutputParameter)
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", " << argumentName << ");" << std::endl;
			else
				errs() << "Error: MonoObject type not supported as input. Ignoring. \n";
		}
		break;
		case ExportedClassTypeCategory::GUIElement:
		{
			const std::string scriptType = GetScriptInteropTypeName(parameterTypeName);

			if(returnValue || isOutputParameter)
				errs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
			else
			{
				const std::string scriptName = "script" + parameterName;

				preCallActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, parameterName, parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argumentName << " = " << GenerateGetInternalCallLine(parameterInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}
		}
			break;
		case ExportedClassTypeCategory::Class:
		case ExportedClassTypeCategory::ReflectableClass:
		{
			const std::string scriptType = GetScriptInteropTypeName(parameterTypeName);

			if (returnValue)
				postCallActions << GenerateNativeClassToMonoObject(parameterInformation.TypeInformation, parameterName, scriptType, argumentName);
			else if (isOutputParameter)
				postCallActions << GenerateNativeClassToMonoObject(parameterInformation.TypeInformation, parameterName, scriptType, argumentName, true);
			else
			{
				const std::string scriptName = "script" + parameterName;
				
				preCallActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, parameterName, parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argumentName << " = " << GenerateGetInternalCallLine(parameterInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}
		}
			break;
		default: // Some resource or game object type
		{
			if (returnValue || isOutputParameter)
			{
				postCallActions << GenerateNativeHandleToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, argumentName, "", "script" + parameterName, parameterName, isOutputParameter);
			}
			else
			{
				const std::string scriptName = "script" + parameterName;
				const std::string scriptType = GetScriptInteropTypeName(parameterTypeName, parameterInformation.TypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));

				preCallActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, parameterName, parameterInformation.TypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argumentName << " = " << GenerateGetInternalCallLine(parameterInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}
		}
		break;
		}

		return argumentName;
	}

	// Handle array types
	assert(parameterInformation.TypeInformation.IsArrayOrVector());
	{
		const VariableTypeInformation& arrayElementTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		std::string arrayEntryTypeName;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			arrayEntryTypeName = parameterTypeName;
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			arrayEntryTypeName = "MonoObject*";
			break;
		default: // Some object or struct type
			arrayEntryTypeName = GetScriptInteropTypeName(parameterTypeName, arrayElementTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));
			break;
		}

		const std::string arrayArgumentType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
		const std::string arrayArgumentName = "vec" + parameterName;

		preCallActions << "\t\t" << arrayArgumentType << " " << arrayArgumentName;
		if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Array)
			preCallActions << "[" << parameterInformation.TypeInformation.ArraySize << "]";
		preCallActions << ";\n";

		if (!isOutputParameter && !returnValue)
		{
			const std::string scriptArrayName = "array" + parameterName;

			preCallActions << "\t\tif(" << parameterName << " != nullptr)\n";
			preCallActions << "\t\t{\n";

			preCallActions << "\t\t\tScriptArray " << scriptArrayName << "(" << parameterName << ");\n";

			if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Vector || parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::SmallVector)
				preCallActions << "\t\t\t" << arrayArgumentName << ".resize(" << scriptArrayName << ".Size());\n";

			preCallActions << "\t\t\tfor(int i = 0; i < (int)" << scriptArrayName << ".Size(); i++)\n";
			preCallActions << "\t\t\t{\n";

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ExportedClassTypeCategory::Primitive:
			case ExportedClassTypeCategory::String:
			case ExportedClassTypeCategory::WString:
			case ExportedClassTypeCategory::Path:
				preCallActions << "\t\t\t\t" << arrayArgumentName << "[i] = " << scriptArrayName << ".Get<" << arrayEntryTypeName << ">(i);" << std::endl;
				break;
			case ExportedClassTypeCategory::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preCallActions << "\t\t\t\t" << arrayArgumentName << "[i] = (" << arrayEntryTypeName << ")" << scriptArrayName << ".Get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ExportedClassTypeCategory::Struct:

				preCallActions << "\t\t\t\t" << arrayArgumentName << "[i] = ";

				if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					preCallActions << arrayEntryTypeName << "::FromInterop(";
					preCallActions << scriptArrayName << ".Get<" << GetStructInteropTypeName(parameterTypeName) << ">(i)";
					preCallActions << ")";
				}
				else
					preCallActions << scriptArrayName << ".Get<" << parameterTypeName << ">(i)";

				preCallActions << ";\n";

				break;
			default: // Some object type
			{
				std::string scriptName = "script" + parameterName;

				preCallActions << GenerateMonoToScriptObject("\t\t\t\t", arrayEntryTypeName, scriptName, scriptArrayName + ".Get<MonoObject*>(i)", arrayElementTypeInformation, parameterTypeMappingInformation);
				preCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preCallActions << "\t\t\t\t{\n";

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + parameterName;

				preCallActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					GenerateGetInternalCallLine(arrayElementTypeInformation, parameterTypeMappingInformation, scriptName) << ";\n";

				if(parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass)
				{
					if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
					{
						if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
						{
							errs() << "Error: Cannot pass Shared<T> by pointer.";
						}

						preCallActions << "\t\t\t\t\t" << arrayArgumentName << "[i] = " << elemPtrName << ";\n";
					}
					else
					{
						if(arrayElementTypeInformation.TypeCategory != VariableTypeCategory::General)
						{
							errs() << "Error: Class passed as an invalid type: " << (uint32_t)arrayElementTypeInformation.TypeCategory;
						}
						
						if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
							preCallActions << "\t\t\t\t\t" << arrayArgumentName << "[i] = " << elemPtrName << ".Get();\n";
						else
						{
							preCallActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
							preCallActions << "\t\t\t\t\t\t" << arrayArgumentName << "[i] = *" << elemPtrName << ";\n";
						}
					}
				}
				else
					preCallActions << "\t\t\t\t\t" << arrayArgumentName << "[i] = " << elemPtrName << ";\n";

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
			const std::string scriptArrayName = "array" + parameterName;

			postCallActions << "\t\tint arraySize" << parameterName << " = ";
			if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Vector || parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::SmallVector)
				postCallActions << "(int)" << arrayArgumentName << ".size()";
			else
				postCallActions << parameterInformation.TypeInformation.ArraySize;
			postCallActions << ";\n";

			postCallActions << "\t\tScriptArray " << scriptArrayName;
			postCallActions << " = " << "ScriptArray::Create<" << arrayEntryTypeName << ">(arraySize" << parameterName << ");" << std::endl;
			postCallActions << "\t\tfor(int i = 0; i < arraySize" << parameterName << "; i++)" << std::endl;
			postCallActions << "\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ExportedClassTypeCategory::Primitive:
			case ExportedClassTypeCategory::String:
			case ExportedClassTypeCategory::WString:
			case ExportedClassTypeCategory::Path:
				postCallActions << "\t\t\t" << scriptArrayName << ".Set(i, " << arrayArgumentName << "[i]);" << std::endl;
				break;
			case ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::Flags)
					postCallActions << "\t\t\t" << scriptArrayName << ".Set(i, (" << enumType << ")(uint32_t)" << arrayArgumentName << "[i]);" << std::endl;
				else
					postCallActions << "\t\t\t" << scriptArrayName << ".Set(i, (" << enumType << ")" << arrayArgumentName << "[i]);" << std::endl;
				break;
			}
			case ExportedClassTypeCategory::Struct:
				postCallActions << "\t\t\t" << scriptArrayName << ".Set(i, ";

				if(arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					postCallActions << arrayEntryTypeName << "::ToInterop(";

				postCallActions << arrayArgumentName << "[i]";

				if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					postCallActions << ")";

				postCallActions << ");\n";

				break;
			case ExportedClassTypeCategory::MonoObject:
				postCallActions << "\t\t\t" << scriptArrayName << ".Set(i, " << arrayArgumentName << "[i]);" << std::endl;
				break;
			case ExportedClassTypeCategory::Class:
			case ExportedClassTypeCategory::ReflectableClass:
			{
				const std::string arrayElementName = "arrayElem" + parameterName;

				const std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				const std::string elemPtrName = "arrayElemPtr" + parameterName;

				postCallActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
				{
					if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
					{
						errs() << "Error: Cannot pass Shared<T> by pointer.";
					}

					postCallActions << " = " << arrayArgumentName << "[i];\n";
				}
				else
				{
					if(arrayElementTypeInformation.TypeCategory != VariableTypeCategory::General)
					{
						errs() << "Error: Class passed as an invalid type: " << (uint32_t)arrayElementTypeInformation.TypeCategory;
					}

					postCallActions << " = bs_shared_ptr_new<" << arrayElementTypeInformation.GetLastWrappedOrSelfTypeName() << ">();\n";

					if (arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						postCallActions << "\t\t\tif(" << arrayArgumentName << "[i])\n";
						postCallActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						postCallActions << "\t\t\t*" << elemPtrName << " = ";
					}

					postCallActions << arrayArgumentName << "[i];\n";
				}

				postCallActions << "\t\t\tMonoObject* " << arrayElementName << ";\n";
				postCallActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, arrayElementName, arrayEntryTypeName, elemPtrName, false, "\t\t\t");

				postCallActions << "\t\t\t" << scriptArrayName << ".Set(i, " << arrayElementName << ");" << std::endl;
				break;
			}
			case ExportedClassTypeCategory::GUIElement:
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
				break;
			default: // Some resource or game object type
			{
				postCallActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, arrayArgumentName, "i", "script" + parameterName, scriptArrayName, false, "\t\t\t");
			}
			break;
			}

			postCallActions << "\t\t}" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << scriptArrayName << ".GetInternal();" << std::endl;
			else
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", (MonoObject*)" << scriptArrayName << ".GetInternal());" << std::endl;
		}

		return arrayArgumentName;
	}
}

/**
 * Generates code that converts a struct field from or to interop.
 *
 * @param name					Name of the field that's being converted.
 * @param fieldInformation		Information about the field being converted.
 * @param toInterop				True if converting native to interop, false if converting from interop to native.
 * @param preActions			Stream that can be used for appending code that will execute before the field assignment happens.
 * @return						Name of the variable containing either the native or interop representation of the field.
 */
std::string GenerateFieldConvertBlock(const std::string& name, const VariableBase& fieldInformation, bool toInterop, std::stringstream& preActions)
{
	TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);

	if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
	{
		outs() << "Error: AsyncOp type not supported as a struct field. \n";
		return "";
	}

	const std::string& fieldTypeName = fieldInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

	// Handle non-array types
	if (!fieldInformation.TypeInformation.IsArrayOrVector())
	{
		std::string arg;

		// Primitive, enum and non-complex structs can be passed as-is.
		// All other types need conversion to the corresponding Mono type.
		const bool isTemporaryRequired = parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Primitive && parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Enum &&
			(parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Struct || fieldInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed));

		if(isTemporaryRequired)
		{
			arg = "tmp" + name;
		}

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
		case ExportedClassTypeCategory::Enum:
			arg = "value." + name;
			break;
		case ExportedClassTypeCategory::Struct:
			if(fieldInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
			{
				std::string interopType = GetStructInteropTypeName(fieldTypeName);
				std::string scriptType = GetScriptInteropTypeName(fieldTypeName);

				if(toInterop)
				{
					preActions << "\t\t" << interopType << " " << arg << ";\n";
					preActions << "\t\t" << arg << " = " << scriptType << "::ToInterop(value." << name << ");\n";
				}
				else
				{
					preActions << "\t\t" << fieldTypeName << " " << arg << ";\n";
					preActions << "\t\t" << arg << " = " << scriptType << "::FromInterop(value." << name << ");\n";
				}
			}
			else
				arg = "value." + name;
			break;
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::Path:
		{
			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";\n";
				preActions << "\t\t" << arg << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, "value." + name) << ";\n";
			}
			else
			{
				preActions << "\t\tString " << arg << ";\n";
				preActions << "\t\t" << arg << " = " << GenerateMonoToStringCall(parameterTypeMappingInformation.TypeCategory, "value." + name) << ";\n";
			}
		}
		break;
		case ExportedClassTypeCategory::MonoObject:
		{
			preActions << "\t\tMonoObject* " << arg << ";" << std::endl;
			preActions << "\t\t" << arg << " = " << name << ";" << std::endl;
		}
		break;
		case ExportedClassTypeCategory::GUIElement:
		{
			const std::string scriptType = GetScriptInteropTypeName(fieldTypeName);

			if(!toInterop)
			{
				if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
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
		case ExportedClassTypeCategory::Class:
		case ExportedClassTypeCategory::ReflectableClass:
		{
			std::string scriptType = GetScriptInteropTypeName(fieldTypeName);

			if(toInterop)
			{
				preActions << "\t\tMonoObject* " << arg << ";\n";

				if(fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
				{
					if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
					{
						errs() << "Error: Invalid struct member type for \"" << name << "\". Cannot pass Shared<T> by pointer.\n";
					}

					preActions << GenerateNativeClassToMonoObject(fieldInformation.TypeInformation, arg, scriptType, "value." + name);
				}
				else
				{
					if(fieldInformation.TypeInformation.TypeCategory != VariableTypeCategory::General)
					{
						errs() << "Error: Invalid struct member type for \"" << name << "\"\n";
					}
					
					std::string tmpType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
					preActions << "\t\t" << tmpType << " " << arg << "copy;\n";

					// Note: Assuming a copy constructor exists
					if (fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						preActions << "\t\tif(value." << name << " != nullptr)\n";
						preActions << "\t\t\t" << arg << "copy = bs_shared_ptr_new<" << fieldTypeName << ">(*value." << name << ");\n";
					}
					else
						preActions << "\t\t" << arg << "copy = bs_shared_ptr_new<" << fieldTypeName << ">(value." << name << ");\n";

					preActions << GenerateNativeClassToMonoObject(fieldInformation.TypeInformation, arg, scriptType, arg + "copy");
				}
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
				if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::General)
				{
					if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						arg = arg + ".get()";
					}
					else
					{
						preActions << "\t\tif(" << arg << " != nullptr)" << std::endl;
						arg = "*" + arg;
					}
				}
				else if(fieldInformation.TypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
					errs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		default: // Some resource or game object type
		{
			std::string scriptType = GetScriptInteropTypeName(fieldTypeName, fieldInformation.TypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));
			std::string scriptName = "script" + name;

			if(toInterop)
			{
				std::string argName;
				
				if(fieldInformation.TypeInformation.TypeCategory != VariableTypeCategory::ComponentOrActor)
					argName = "value." + name;
				else
					argName = "value." + name + ".GetComponent()";

				preActions << "\t\tMonoObject* " << arg << ";\n";
				preActions << GenerateNativeHandleToMonoObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, argName, "", scriptName, arg, false);
			}
			else
			{
				std::string tmpType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;
				
				preActions << GenerateMonoToScriptObject("\t\t", scriptType, scriptName, "value." + name, fieldInformation.TypeInformation, parameterTypeMappingInformation);
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << GenerateGetInternalCallLine(fieldInformation.TypeInformation, parameterTypeMappingInformation, scriptName) << ";" << std::endl;
			}

			const VariableTypeInformation& underlyingType = fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::ComponentOrActor ? fieldInformation.TypeInformation.AssertGetUnderlyingType() : fieldInformation.TypeInformation;
			if(underlyingType.TypeCategory != VariableTypeCategory::GameObjectHandle && underlyingType.TypeCategory != VariableTypeCategory::ResourceHandle)
				outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
		}
		break;
		}

		return arg;
	}

	// Handle array types
	assert(fieldInformation.TypeInformation.IsArrayOrVector());
	{
		const VariableTypeInformation& arrayElementTypeInformation = fieldInformation.TypeInformation.AssertGetUnderlyingType();

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
			entryType = GetScriptInteropTypeName(fieldTypeName, arrayElementTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));
			break;
		}

		const std::string argType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
		const std::string argName = "vec" + name;

		if (!toInterop)
		{
			const std::string scriptArrayName = "array" + name;
			preActions << "\t\t" << argType << " " << argName;
			if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::Array)
				preActions << "[" << fieldInformation.TypeInformation.ArraySize << "]";
			preActions << ";" << std::endl;

			preActions << "\t\tif(value." << name << " != nullptr)\n";
			preActions << "\t\t{\n";
			preActions << "\t\t\tScriptArray " << scriptArrayName << "(value." << name << ");\n";

			if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::SmallVector || fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::Vector)
				preActions << "\t\t\t" << argName << ".resize(" << scriptArrayName << ".Size());\n";

			preActions << "\t\t\tfor(int i = 0; i < (int)" << scriptArrayName << ".Size(); i++)" << std::endl;
			preActions << "\t\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preActions << "\t\t\t\t" << argName << "[i] = " << scriptArrayName << ".Get<" << entryType << ">(i);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << scriptArrayName << ".get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				preActions << "\t\t\t\t" << argName << "[i] = ";

				if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					preActions << entryType << "::FromInterop(";
					preActions << scriptArrayName << ".Get<" << GetStructInteropTypeName(fieldTypeName) << ">(i)";
					preActions << ")";
				}
				else
					preActions << scriptArrayName << ".Get<" << fieldTypeName << ">(i)";

				preActions << ";\n";
				break;
			default: // Some object type
			{
				std::string scriptName = "script" + name;
				preActions << GenerateMonoToScriptObject("\t\t\t\t", entryType, scriptName, scriptArrayName + ".Get<MonoObject*>(i)", fieldInformation.TypeInformation, parameterTypeMappingInformation);
				
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t{\n";

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					GenerateGetInternalCallLine(arrayElementTypeInformation, parameterTypeMappingInformation, scriptName) << ";\n";

				if(parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class || parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
				{
					// Cast from SPtr to the destination type
					if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::General)
					{
						if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
						{
							preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".get();\n";
						}
						else
						{
							preActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
							preActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
						}
					}
					else
					{
						if (arrayElementTypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
							errs() << "Error: Invalid struct member type for \"" << name << "\"\n";

						if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
							errs() << "Error: Invalid struct member type for \"" << name << "\"\n";

						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
					}
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
			if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::Vector || fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::SmallVector)
				preActions << "(int)value." << name << ".size()";
			else
				preActions << fieldInformation.TypeInformation.ArraySize;
			preActions << ";\n";

			preActions << "\t\tMonoArray* " << argName << ";" << std::endl;

			const std::string scriptArrayName = "array" + name;
			preActions << "\t\tScriptArray " << scriptArrayName;
			preActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
			preActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
			preActions << "\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preActions << "\t\t\t" << scriptArrayName << ".Set(i, value." << name << "[i]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preActions << "\t\t\t" << scriptArrayName << ".Set(i, (" << enumType << ")value." << name << "[i]);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				preActions << "\t\t\t" << scriptArrayName << ".Set(i, ";

				if(arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					preActions << entryType << "::ToInterop(";

				preActions << "value." << name << "[i]";

				if(arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					preActions << ")";

				preActions << ");\n";
				break;
			case ::ExportedClassTypeCategory::MonoObject:
				preActions << "\t\t\t" << scriptArrayName << ".Set(i, value." << name << "[i]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Class:
			case ::ExportedClassTypeCategory::ReflectableClass:
			{
				std::string elemName = "arrayElem" + name;

				std::string elemPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::General)
				{
					preActions << " = bs_shared_ptr_new<" << fieldTypeName << ">();\n";

					if (arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
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
				{
					if (arrayElementTypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
						errs() << "Error: Invalid struct member type for \"" << name << "\"\n";

					if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
						errs() << "Error: Invalid struct member type for \"" << name << "\"\n";

					preActions << " = value." << name << "[i];\n";
				}

				preActions << "\t\t\tMonoObject* " << elemName << ";\n";
				preActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, elemName, entryType, elemPtrName, false, "\t\t\t");

				preActions << "\t\t\t" << scriptArrayName << ".Set(i, " << elemName << ");" << std::endl;
			}
			break;
			case ::ExportedClassTypeCategory::GUIElement:
				// Unsupported as output
				break;
			default: // Some resource or game object type
			{
				preActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, "value." + name, "i", "script" + name, scriptArrayName, false, "\t\t\t");
			}
			break;
			}

			preActions << "\t\t}" << std::endl;
			preActions << "\t\t" << argName << " = " << scriptArrayName << ".GetInternal();" << std::endl;
		}

		return argName;
	}
}

/**
 * Generates code required for passing an argument from event callback to an event thunk.
 *
 * @param name						Name of the argument variable received in the event callback.
 * @param parameterInformation		Information about the parameter the argument is being passed to.
 * @param preCallActions			Actions to execute before the thunk call.
 * @return							Name of the argument to pass to the thunk.
 */
std::string GenerateEventCallbackBodyBlockForArgument(const std::string& name, const VariableBase& parameterInformation, std::stringstream& preCallActions)
{
	const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);
	const std::string& parameterTypeName = parameterInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

	if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
	{
		errs() << "Error: AsyncOp type not supported as an event callback parameter. \n";
		return "";
	}

	// Handle non-array types
	if (!parameterInformation.TypeInformation.IsArrayOrVector())
	{
		std::string argName;

		// Primitives and non-flags enums can be passed as-is.
		// All other types need conversion to the corresponding Mono type.
		const bool isTemporaryRequired =
			parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Primitive &&
			(parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Enum || parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags);

		if(isTemporaryRequired)
		{
			argName = "tmp" + name;
		}

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
			argName = name;
			break;
		case ::ExportedClassTypeCategory::Enum:
			if(parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
			{
				preCallActions << "\t\t" << parameterTypeName << argName << ";" << std::endl;
				preCallActions << "\t\t" << argName << " = (" << parameterTypeName << ")(uint32_t)" << name << ";" << std::endl;
			}
			else
				argName = name;
			break;
		case ::ExportedClassTypeCategory::Struct:
			{
				const std::string scriptType = GetScriptInteropTypeName(parameterTypeName);
				preCallActions << "\t\tMonoObject* " << argName << ";\n";

				if(parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					const std::string interopName = "interop" + name;
					const std::string interopType = GetStructInteropTypeName(parameterTypeName);
					
					preCallActions << "\t\t" << interopType << " " << interopName << ";" << std::endl;
					preCallActions << "\t\t" << interopName << " = " << scriptType << "::ToInterop(" << name << ");" << std::endl;
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << interopName << ");\n";
				}
				else
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << name << ");\n";
			}

			break;
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		{
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, name) << ";\n";
		}
		break;
		break;
		case ::ExportedClassTypeCategory::MonoObject:
		{
			preCallActions << "\t\tMonoObject* " << argName << " = " << name << ";\n";
		}
		break;
		case ::ExportedClassTypeCategory::Class:
		case ::ExportedClassTypeCategory::ReflectableClass:
		{
			const std::string scriptType = GetScriptInteropTypeName(parameterTypeName);

			preCallActions << "\t\tMonoObject* " << argName << ";\n";
			preCallActions << GenerateNativeClassToMonoObject(parameterInformation.TypeInformation, argName, scriptType, name);
		}
			break;
		default: // Some resource or game object type
		{
			preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
			preCallActions << GenerateNativeHandleToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, name, "", "script" + name, argName, false);
		}
		break;
		}

		return argName;
	}

	// Handle array types
	assert(parameterInformation.TypeInformation.IsArrayOrVector());
	{
		const VariableTypeInformation& arrayElementTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		std::string entryType;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			entryType = parameterTypeName;
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = GetScriptInteropTypeName(parameterTypeName, getPassAsResourceRef(parameterInformation.flags));
			break;
		}

		std::string argName = "vec" + name;
		preCallActions << "\t\tMonoArray* " << argName << ";" << std::endl;

		preCallActions << "\t\tint arraySize" << name << " = ";
		if (isVector(parameterInformation.flags) || isSmallVector(parameterInformation.flags))
			preCallActions << "(int)value." << name << ".size()";
		else
			preCallActions << parameterInformation.arraySize;
		preCallActions << ";\n";

		std::string arrayName = "array" + name;
		preCallActions << "\t\tScriptArray " << arrayName;
		preCallActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
		preCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
		preCallActions << "\t\t{" << std::endl;

		switch (parameterTypeMappingInformation.TypeCategory)
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
			ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

			if(isFlagsEnum(parameterInformation.flags))
				preCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")(uint32_t)" << name << "[i]);" << std::endl;
			else
				preCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")" << name << "[i]);" << std::endl;
			break;
		}
		case ::ExportedClassTypeCategory::Struct:
			preCallActions << "\t\t\t" << arrayName << ".Set(i, ";

			if (isComplexStruct(parameterInformation.flags))
				preCallActions << entryType << "::ToInterop(";

			preCallActions << name << "[i]";

			if (isComplexStruct(parameterInformation.flags))
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
			preCallActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, elemName, entryType, name + "[i]", false, "\t\t\t");
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
		}
		break;
		default: // Some resource or game object type
		{
			preCallActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, name, "i", "script" + name, arrayName, false, "\t\t\t");
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
		const std::string argumentName = GenerateEventCallbackBodyBlockForArgument(I->Name, *I, preCallActions);
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
				std::string argName = GenerateFieldConvertBlock(fieldInfo.Name, fieldInfo, false, output);

				output << "\t\tauto tmp" << fieldInfo.Name << " = " << argName << ";\n";
				output << "\t\tfor(int i = 0; i < " << fieldInfo.arraySize << "; ++i)\n";
				output << "\t\t\toutput." << fieldInfo.Name << "[i] = tmp" << fieldInfo.Name << "[i];\n";
			}
			else
			{
				std::string argName = GenerateFieldConvertBlock(fieldInfo.Name, fieldInfo, false, output);

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
			std::string argName = GenerateFieldConvertBlock(fieldInfo.Name, fieldInfo, true, output);

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
