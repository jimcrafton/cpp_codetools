#pragma once

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <memory>
#include <string>
#include <vector>

namespace cpptools_analysis {

// Runs `action` over `content` as the file `path`: the text stands in for the file on disk, everything it
// includes is read from disk. Diagnostics are dropped; a file with errors is still analyzed as far as clang gets.
inline bool runAnalysisTool(std::unique_ptr<clang::FrontendAction> action, const std::string& content,
                            const std::string& path, const std::vector<std::string>& args) {
    llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> overlay(
        new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));
    llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> memory(new llvm::vfs::InMemoryFileSystem);
    overlay->pushOverlay(memory);
    llvm::IntrusiveRefCntPtr<clang::FileManager> files(new clang::FileManager(clang::FileSystemOptions(), overlay));
    memory->addFile(path, 0, llvm::MemoryBuffer::getMemBufferCopy(content));

    std::vector<std::string> commandLine{ "clang-tool", "-fsyntax-only" };
    for (const std::string& arg : args) {
        if (arg != "-fsyntax-only") commandLine.push_back(arg);
    }
    commandLine.push_back(path);

    clang::IgnoringDiagConsumer ignore;
    clang::tooling::ToolInvocation invocation(commandLine, std::move(action), files.get());
    invocation.setDiagnosticConsumer(&ignore);
    return invocation.run();
}

}  // namespace cpptools_analysis
