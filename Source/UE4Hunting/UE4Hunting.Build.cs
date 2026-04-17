// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE4Hunting : ModuleRules
{
    public UE4Hunting(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // --- 运行时通用模块 (已移除缺失的插件模块) ---
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "Slate",
            "SlateCore",
            "GameplayAbilities",
            "GameplayTags",
            "GameplayTasks",
            "MoviePlayer",
            "AsyncLoadingScreen",
            "MovieScene",
            "LevelSequence",
            "Json",
            "UMG",
            "AssetRegistry",
            "Sockets",
            "Networking",
            "Niagara",
            "SequencerScripting",
            "HeadMountedDisplay",
            "XRBase",
            "ProSceneCapture2D",
            "DLSSBlueprint",
            "StreamlineBlueprint",
            "StreamlineReflexBlueprint",
            "StreamlineDLSSGBlueprint",
            "StreamlineRHI"
        });

        // --- 编辑器专用模块 ---
        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "Blutility",
                "UnrealEd",
                "AssetTools",
                "EditorScriptingUtilities"
            });
        }

        // --- 平台特定配置 ---
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("User32.lib");
            PublicSystemLibraries.Add("Shell32.lib");
            PublicSystemLibraries.Add("Ole32.lib");
            PublicDefinitions.Add("WIN32_LEAN_AND_MEAN");
            PublicDefinitions.Add("NOMINMAX");
        }

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}