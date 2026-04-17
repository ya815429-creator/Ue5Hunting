// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UE4HuntingEditorTarget : TargetRules
{
    public UE4HuntingEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        // 升级到 V5 适配 UE 5.5
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        // 核心修复：显式开启严格符合模式
        WindowsPlatform.bStrictConformanceMode = true;

        ExtraModuleNames.Add("UE4Hunting");
    }
}