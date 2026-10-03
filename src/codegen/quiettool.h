#pragma once

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <memory>
#include <string>

namespace cpptools_codegen {

// Same as clang::tooling::runToolOnCode, but diagnostics are discarded instead of printed to stderr.
// The controller header is parsed on its own with no include paths, so its #includes always fail -
// expected, and the AST matchers still work on what clang recovers.
inline bool runToolOnCodeQuietly(std::unique_ptr<clang::FrontendAction> action, const std::string& code) {
    llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> overlay(
        new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));
    llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> memory(new llvm::vfs::InMemoryFileSystem);
    overlay->pushOverlay(memory);
    llvm::IntrusiveRefCntPtr<clang::FileManager> files(new clang::FileManager(clang::FileSystemOptions(), overlay));

    const std::string fileName = "input.cc";
    memory->addFile(fileName, 0, llvm::MemoryBuffer::getMemBufferCopy(code));

    clang::IgnoringDiagConsumer ignore;
    clang::tooling::ToolInvocation invocation({"clang-tool", "-fsyntax-only", fileName}, std::move(action), files.get());
    invocation.setDiagnosticConsumer(&ignore);
    return invocation.run();
}

} // namespace cpptools_codegen
