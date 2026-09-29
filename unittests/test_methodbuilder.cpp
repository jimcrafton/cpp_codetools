#include <gtest/gtest.h>

#include "cpptools_codegen/methodbuilder.h"

using namespace cpptools_codegen;

TEST(MethodBuilderTest, GeneratesAPlainVoidMethodWithNoArguments) {
    MethodBuilder builder("doThing");

    EXPECT_EQ(builder.toString(), "void doThing() {\n}\n");
}

TEST(MethodBuilderTest, OrdersModifiersCorrectlyRegardlessOfCallOrder) {
    MethodBuilder builder("getValue");
    // Set in reverse of C++'s own required order - toString() must still emit them correctly.
    builder.makeOverride().makeNoexcept().makeConst().makeVirtual().makeInline().makeStatic();
    builder.returnType("int");

    EXPECT_EQ(builder.toString(), "static virtual inline int getValue() const noexcept override {\n}\n");
}

TEST(MethodBuilderTest, IncludesTemplateParametersAndArgumentsWithDefaults) {
    MethodBuilder builder("clamp");
    builder.returnType("T")
        .addTemplateParam("typename T")
        .addArgument("T", "value")
        .addArgument("T", "low", "T(0)")
        .addBodyLine("return value < low ? low : value;");

    EXPECT_EQ(builder.toString(),
              "template <typename T>\n"
              "T clamp(T value, T low = T(0)) {\n"
              "    return value < low ? low : value;\n"
              "}\n");
}

TEST(MethodBuilderTest, IndentsTheSignatureAndBodyOneLevelApart) {
    MethodBuilder builder("run");
    builder.addBodyLine("doWork();");

    EXPECT_EQ(builder.toString(1),
              "    void run() {\n"
              "        doWork();\n"
              "    }\n");
}

TEST(MethodBuilderTest, ArgumentRawDeclaratorIsUsedVerbatimInPlaceOfTypeAndName) {
    // A pointer-to-member-function parameter wraps its name *inside* the type
    // ("Ret (T::*name)(Args...)") - the plain type+name composition can't express that.
    MethodBuilder builder("add");
    builder.addArgument(Argument::raw("SyncReturn (T::*method)(SenderRefT, Args...)"));

    EXPECT_EQ(builder.toString(), "void add(SyncReturn (T::*method)(SenderRefT, Args...)) {\n}\n");
}

// Reproduces newui::Delegate<SenderT,Args...>::add()'s own signature - the worked example from
// bluesky/cpp-codegen-plan.md/"c++ code gen.docx" that proved the Fluent Builder approach scales
// to this project's actual signatures, not just toy examples. Built with the general
// MethodBuilder/Argument, not a bespoke ConnectionFactoryMethodBuilder - the whole point of the
// general builder is not needing one class per signature shape.
TEST(MethodBuilderTest, ReproducesTheDelegateAddSignatureShape) {
    MethodBuilder builder("add");
    builder.returnType("Connection")
        .addTemplateParam("typename T")
        .addArgument("std::string", "descriptor")
        .addArgument("T*", "instance")
        .addArgument(Argument::raw("SyncReturn (T::*method)(SenderRefT, Args...)"))
        .addBodyLine("if (instance == nullptr || method == nullptr) {")
        .addBodyLine("    return Connection();")
        .addBodyLine("}")
        .addBodyLine("return add(std::move(descriptor), [instance, method](SenderRefT sender, Args... args) {")
        .addBodyLine("    return (instance->*method)(sender, args...);")
        .addBodyLine("});");

    EXPECT_EQ(builder.toString(),
              "template <typename T>\n"
              "Connection add(std::string descriptor, T* instance, SyncReturn (T::*method)(SenderRefT, Args...)) {\n"
              "    if (instance == nullptr || method == nullptr) {\n"
              "        return Connection();\n"
              "    }\n"
              "    return add(std::move(descriptor), [instance, method](SenderRefT sender, Args... args) {\n"
              "        return (instance->*method)(sender, args...);\n"
              "    });\n"
              "}\n");
}
