#include "B3DCommon.h"
#include "B3DDocGenVisitor.h"
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
const char *const kBuiltinGUIElementType = "GUIElement";
const char *const kBuiltinReflectableType = "IReflectable";
const char *const kBuiltinIScriptExportableType = "IScriptExportable";

std::string sFrameworkCppNs = "b3d";
std::string sEditorCppNs = "b3d";
std::string sFrameworkCsNs = "b3d";
std::string sEditorCsNs = "b3d.Editor";
std::string sFrameworkDllExportMacro = "B3D_SCRIPT_INTEROP_EXPORT";
std::string sEditorDllExportMacro = "B3D_EDITOR_SCRIPT_INTEROP_EXPORT";
std::string sFrameworkCopyrightNotice = 
	"//************************************ B3D Framework - Copyright 2025 Marko Pintera **************************************//\n" \
	"//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//\n";
std::string sEditorCopyrightNotice = 
	"//********************************** Banshee Engine (www.banshee3d.com) **************************************************//\n" \
	"//****************** Copyright (c) 2025 Marko Pintera (marko.pintera@gmail.com). All rights reserved. ********************//\n";

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

static cl::opt<bool> DocGenJsonOption(
	"docgen-json",
	cl::desc("If enabled the tool emits a documentation JSON file describing every class, struct, enum, method, field and free function in the processed translation unit, instead of generating script bindings.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> DocGenJsonOutputOption(
	"docgen-json-output",
	cl::desc("Path to the documentation JSON file to emit when -docgen-json is enabled.\n"),
	cl::cat(OptCategory));

class BansheeCodeGeneratorASTConsumer : public ASTConsumer
{
public:
	BansheeCodeGeneratorASTConsumer(CompilerInstance* CI, CommentParser& commentParser)
		: visitor(new BansheeCodeGeneratorASTVisitor(CI, commentParser))
	{ }

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
		: mCommentParser(commentParser)
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

class BansheeDocGeneratorASTConsumer : public ASTConsumer
{
public:
	BansheeDocGeneratorASTConsumer(ASTContext& context, CommentParser& commentParser, const std::string& outputPath)
		: mVisitor(&context, commentParser), mOutputPath(outputPath)
	{ }

	void HandleTranslationUnit(ASTContext& Context) override
	{
		mVisitor.TraverseDecl(Context.getTranslationUnitDecl());
		if (!mVisitor.WriteJSON(mOutputPath))
			mFailed = true;
	}

	bool HasFailed() const { return mFailed; }

private:
	BansheeDocGeneratorASTVisitor mVisitor;
	std::string mOutputPath;
	bool mFailed = false;
};

class BansheeDocGeneratorFrontendAction : public ASTFrontendAction
{
public:
	BansheeDocGeneratorFrontendAction(const std::string& outputPath)
		: mOutputPath(outputPath)
	{ }

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override
	{
		mCommentParser.SetASTContext(CI.getASTContext());
		auto consumer = std::make_unique<BansheeDocGeneratorASTConsumer>(CI.getASTContext(), mCommentParser, mOutputPath);
		mConsumer = consumer.get();
		return consumer;
	}

	bool HasFailed() const { return mConsumer != nullptr && mConsumer->HasFailed(); }

private:
	CommentParser mCommentParser;
	std::string mOutputPath;
	BansheeDocGeneratorASTConsumer* mConsumer = nullptr;
};

class BansheeDocGeneratorFrontendActionFactory : public FrontendActionFactory
{
public:
	explicit BansheeDocGeneratorFrontendActionFactory(const std::string& outputPath)
		: mOutputPath(outputPath)
	{ }

	std::unique_ptr<FrontendAction> create() override
	{
		auto action = std::make_unique<BansheeDocGeneratorFrontendAction>(mOutputPath);
		mLastAction = action.get();
		return action;
	}

	bool HasFailed() const { return mLastAction != nullptr && mLastAction->HasFailed(); }

private:
	std::string mOutputPath;
	BansheeDocGeneratorFrontendAction* mLastAction = nullptr;
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
	
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TMatrix3<float>", "Math/B3DMatrix3.h", "Matrix3", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TMatrix4<float>", "Math/B3DMatrix4.h", "Matrix4", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TRadian<float>", "Math/B3DRadian.h", "Radian", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TDegree<float>", "Math/B3DDegree.h", "Degree", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TCapsule<float>", "Math/B3DCapsule.h", "Capsule", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "TBounds<float>", "Math/B3DBounds.h", "Bounds", "", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "UUID", "Utility/B3DUUID.h", "UUID", "B3DScriptResourceWrapper.h", ExportedClassTypeCategory::Struct);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "GameObject", "Scene/B3DGameObject.h", "GameObject", "B3DScriptGameObjectWrapper.h", ExportedClassTypeCategory::GameObject);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "SceneObject", "Scene/B3DSceneObject.h", "SceneObject", "Wrappers/B3DScriptSceneObject.h", ExportedClassTypeCategory::SceneObject);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Resource", "Resources/B3DResource.h", "Resource", "B3DScriptResourceWrapper.h", ExportedClassTypeCategory::Resource);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "Any", "Utility/B3DAny.h", "Any", "", ExportedClassTypeCategory::Class);
	TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(frameworkNamespace, "GUIContextMenu", "GUI/B3DGUIContextMenu.h", "ContextMenu", "Wrappers/B3DScriptContextMenu.h", ExportedClassTypeCategory::Class);

	// Doc-gen and script binding generation are mutually exclusive per invocation.
	if (DocGenJsonOption.getValue())
	{
		if (DocGenJsonOutputOption.getValue().empty())
		{
			errs() << "Error: -docgen-json requires -docgen-json-output=<path>.\n";
			return 1;
		}

		auto factory = std::make_unique<BansheeDocGeneratorFrontendActionFactory>(DocGenJsonOutputOption.getValue());
		int output = Tool.run(factory.get());
		if (factory->HasFailed())
			return 1;
		return output;
	}

	// Parse C++ into an easy to read format
	auto factory = std::make_unique<BansheeCodeGeneratorFrontendActionFactory>();
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

