// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UE4HuntingTarget : TargetRules
{
    public UE4HuntingTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        // 升级到 V5 适配 UE 5.5
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        // 强制开启严格一致性以匹配引擎
        WindowsPlatform.bStrictConformanceMode = true;

        ExtraModuleNames.Add("UE4Hunting");
    }
}