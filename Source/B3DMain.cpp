#include "B3DCommon.h"
#include "B3DParser.h"
#include "B3DParserUtility.h"
#include "B3DTypeLookup.h"

#define B3DCODEGEN_WAIT_FOR_DEBUGGER 0
#if B3DCODEGEN_WAIT_FOR_DEBUGGER
#include <windows.h>
#endif

const char *const kBuiltinGameObjectType = "GameObject";
const char *const kBuiltinComponentType = "Component";
const char *const kBuiltinSceneObjectType = "SceneObject";
const char *const kBuiltinResourceType = "Resource";
const char *const kBuiltinModuleType = "Module";
const char *const kBuiltinGUIElementType = "GUIInteractable";
const char *const kBuiltinReflectableType = "IReflectable";
const char *const kBuiltinIScriptExportableType = "IScriptExportable";

std::string sFrameworkCppNs = "bs";
std::string sEditorCppNs = "bs";
std::string sFrameworkCsNs = "bs";
std::string sEditorCsNs = "bs.Editor";
std::string sFrameworkDllExportMacro = "B3D_SCRIPT_INTEROP_EXPORT";
std::string sEditorDllExportMacro = "B3D_EDITOR_SCRIPT_INTEROP_EXPORT";
std::string sFrameworkCopyrightNotice = 
	"//********************************* bs::framework - Copyright 2018-2022 Marko Pintera ************************************//\n" \
	"//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//\n";
std::string sEditorCopyrightNotice = 
	"//********************************** Banshee Engine (www.banshee3d.com) **************************************************//\n" \
	"//************** Copyright (c) 2016-2022 Marko Pintera (marko.pintera@gmail.com). All rights reserved. *******************//\n";

static cl::OptionCategory OptCategory("Script binding options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp CustomHelp("\nAdd \"-- <compiler arguments>\" at the end to setup the compiler "
	"invocation\n");

static cl::opt<std::string> OutputCppEngineOption(
	"output-cpp",
	cl::desc("Specify output directory. Generated non-editor CPP files will be placed into that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCppEditorOption(
	"output-cpp-editor",
	cl::desc("Specify output directory. Generated editor CPP files will be placed into that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCSEngineOption(
	"output-cs",
	cl::desc("Specify output directory. Generated non-editor CS files will be placed relative to that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCSEditorOption(
	"output-cs-editor",
	cl::desc("Specify output directory. Generated editor CS files will be placed relative to that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<bool> GenerateEditorOption(
	"gen-editor",
	cl::desc("If enabled the script code marked with BED API will be generated as well.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkNamespaceOption(
	"cpp-framework-ns",
	cl::desc("Specify namespace to place generated C++ framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorNamespaceOption(
	"cpp-editor-ns",
	cl::desc("Specify namespace to place generated C++ editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CsFrameworkNamespaceOption(
	"cs-framework-ns",
	cl::desc("Specify namespace to place generated C# framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CsEditorNamespaceOption(
	"cs-editor-ns",
	cl::desc("Specify namespace to place generated C# editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkExportMacroOption(
	"cpp-framework-export-macro",
	cl::desc("Specify DLL export macro to use for generated C++ framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorExportMacroOption(
	"cpp-editor-export-macro",
	cl::desc("Specify DLL export macro to use for generated C++ editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkCopyrightNoticeOption(
	"framework-copyright-notice",
	cl::desc("Specify copyright notice to add to the header of all generated framework files.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorCopyrightNoticeOption(
	"editor-copyright-notice",
	cl::desc("Specify copyright notice to add to the header of all generated editor files.\n"),
	cl::cat(OptCategory));

class BansheeCodeGeneratorASTConsumer : public ASTConsumer 
{
public:
	explicit BansheeCodeGeneratorASTConsumer(CompilerInstance* CI, CommentParser& commentParser)
		: visitor(new BansheeCodeGeneratorASTVisitor(CI, commentParser))
	{
		
	}

	~BansheeCodeGeneratorASTConsumer()
	{
		delete visitor;
	}

	void HandleTranslationUnit(ASTContext& Context) override
	{
		visitor->TraverseDecl(Context.getTranslationUnitDecl());
	}

private:
	BansheeCodeGeneratorASTVisitor *visitor;
};

class BansheeCodeGeneratorFrontendAction : public ASTFrontendAction 
{
public:
	BansheeCodeGeneratorFrontendAction(CommentParser& commentParser)
		:mCommentParser(commentParser)
	{ }

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override
	{
		mCommentParser.SetASTContext(CI.getASTContext());
		return std::make_unique<BansheeCodeGeneratorASTConsumer>(&CI, mCommentParser);
	}

private:
	CommentParser mCommentParser;
};

class BansheeCodeGeneratorFrontendActionFactory : public FrontendActionFactory
{
public:
	/** Returns a helper class that can be used for parsing comments. */
	CommentParser& GetCommentParser() { return mCommentParser; }

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<BansheeCodeGeneratorFrontendAction>(mCommentParser);
	}

private:
	CommentParser mCommentParser;
};

int main(int argc, const char** argv)
{
#if B3DCODEGEN_WAIT_FOR_DEBUGGER
	// Waits before starting the code-gen, to allow the debugger to attach (Windows only)
	// Requires <windows.h> header
	 while (!::IsDebuggerPresent())
	{
		::Sleep(100);
	}
	::DebugBreak();
#endif

	Expected<CommonOptionsParser> op = CommonOptionsParser::create(argc, argv, OptCategory);
	if(auto error = op.takeError())
	{
		errs() << "Error creating command line options parser: " << toString(std::move(error));
		return 1;
	}

	CommonOptionsParser& optionsParser = *op;
	ClangTool Tool(optionsParser.getCompilations(), optionsParser.getSourcePathList());

	if (!CppFrameworkNamespaceOption.getValue().empty())
		sFrameworkCppNs = std::string(CppFrameworkNamespaceOption.getValue().c_str());
	
	if (!CppEditorNamespaceOption.getValue().empty())
		sEditorCppNs = std::string(CppEditorNamespaceOption.getValue().c_str());
	
	if (!CsFrameworkNamespaceOption.getValue().empty())
		sFrameworkCsNs = std::string(CsFrameworkNamespaceOption.getValue().c_str());
	
	if (!CsEditorNamespaceOption.getValue().empty())
		sEditorCsNs = std::string(CsEditorNamespaceOption.getValue().c_str());
	
	if (!CppFrameworkExportMacroOption.getValue().empty())
		sFrameworkDllExportMacro = std::string(CppFrameworkExportMacroOption.getValue().c_str());
	
	if (!CppEditorExportMacroOption.getValue().empty())
		sEditorDllExportMacro = std::string(CppEditorExportMacroOption.getValue().c_str());

	if (!CppFrameworkCopyrightNoticeOption.empty())
		sFrameworkCopyrightNotice = std::string(CppFrameworkCopyrightNoticeOption.getValue().c_str());
	
	if (!CppEditorCopyrightNoticeOption.empty())
		sEditorCopyrightNotice = std::string(CppEditorCopyrightNoticeOption.getValue().c_str());
	
	// Note: I could auto-generate C++ wrappers for these types
	SmallVector<std::string, 4> frameworkNamespace = { sFrameworkCppNs };
	
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TVector2<float>", "Math/BsVector2.h", "Vector2", "Wrappers/BsScriptVector.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TVector3<float>", "Math/BsVector3.h", "Vector3", "Wrappers/BsScriptVector.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TVector4<float>", "Math/BsVector4.h", "Vector4", "Wrappers/BsScriptVector.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TSize2<float>", "Utility/BsUtil.h", "Size2", "Wrappers/BsScriptSize.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TSize2<uint32_t>", "Utility/BsUtil.h", "Size2UI", "Wrappers/BsScriptSize.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Matrix3", "Math/BsMatrix3.h", "Matrix3", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Matrix4", "Math/BsMatrix4.h", "Matrix4", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Quaternion", "Math/BsQuaternion.h", "Quaternion", "Wrappers/BsScriptQuaternion.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TRadian<float>", "Math/BsRadian.h", "Radian", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TDegree<float>", "Math/BsDegree.h", "Degree", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Color", "Image/BsColor.h", "Color", "Wrappers/BsScriptColor.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "AABox", "Math/BsAABox.h", "AABox", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Sphere", "Math/BsSphere.h", "Sphere", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Capsule", "Math/BsCapsule.h", "Capsule", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Ray", "Math/BsRay.h", "Ray", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Rect2", "Math/BsRect2.h", "Rect2", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Rect2I", "Math/BsRect2I.h", "Rect2I", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Bounds", "Math/BsBounds.h", "Bounds", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Plane", "Math/BsPlane.h", "Plane", "Wrappers/BsScriptPlane.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "UUID", "Utility/BsUUID.h", "UUID", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "GameObject", "Scene/BsGameObject.h", "GameObject", "BsScriptGameObjectWrapper.h", ExportedClassTypeCategory::GameObject);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "SceneObject", "Scene/BsSceneObject.h", "SceneObject", "Wrappers/BsScriptSceneObject.h", ExportedClassTypeCategory::SceneObject);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Resource", "Resources/BsResource.h", "Resource", "BsScriptResourceWrapper.h", ExportedClassTypeCategory::Resource);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Any", "Utility/BsAny.h", "Any", "", ExportedClassTypeCategory::Class);

	// Parse C++ into an easy to read format
	const std::unique_ptr<BansheeCodeGeneratorFrontendActionFactory> factory = std::unique_ptr<BansheeCodeGeneratorFrontendActionFactory>(new BansheeCodeGeneratorFrontendActionFactory);
	int output = Tool.run(factory.get());

	CommentParser& commentParser = factory->GetCommentParser();
	TypeLookup::FinalizeFilesToGenerate(commentParser);

	bool genEditor = GenerateEditorOption.getValue();

	// Generate code
	GenerateCpp(OutputCppEngineOption.getValue(), OutputCppEditorOption.getValue(), genEditor);
	GenerateCSharp(OutputCSEngineOption.getValue(), OutputCSEditorOption.getValue(), genEditor);

	//system("pause");
	return output;
}

