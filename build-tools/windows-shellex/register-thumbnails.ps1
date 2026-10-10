# SPDX-FileCopyrightText: 2026 Solstice contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Registers (or with -Unregister removes) the Krita Shell Extension in this
# folder as the Explorer thumbnail provider of .kra and .krz files, for the
# current Windows user only (HKEY_CURRENT_USER; no administrator rights).
# A machine-wide registration by the Krita installer is left untouched; the
# per-user entries take precedence over it while they exist. File
# associations (the program that opens .kra files) are not changed.
# -DryRun prints the planned registry changes without making them.
# See docs/windows-shell-thumbnails.md.
# -TestRoot is for the scripts' own tests: it moves every key under
# HKCU\<TestRoot> instead of HKCU\Software, where Explorer does not look.
param([switch]$Unregister, [switch]$DryRun, [string]$TestRoot = '')

$ErrorActionPreference = 'Stop'
$Here = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$ThumbnailClsid = '{C6806289-D605-4AFE-A778-BC584303DB9A}'
$ThumbnailProviderIid = '{E357FCCD-A995-4576-B01F-234630154E96}'
$Extensions = @('.kra', '.krz')
$Base = if ($TestRoot) { $TestRoot } else { 'Software' }
$MarkerPath = "$Base\Solstice\ShellExtension"
$Views = @(
    @{ View = [Microsoft.Win32.RegistryView]::Registry64; Dll = 'kritashellex64.dll'; Name = '64-bit' },
    @{ View = [Microsoft.Win32.RegistryView]::Registry32; Dll = 'kritashellex32.dll'; Name = '32-bit' }
)

function Open-UserRoot([Microsoft.Win32.RegistryView]$View) {
    [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $View)
}

function Get-ViewLabel([Microsoft.Win32.RegistryView]$View) {
    if ($View -eq [Microsoft.Win32.RegistryView]::Registry32) { return ' (32-bit view)' }
    return ''
}

function Set-Value([Microsoft.Win32.RegistryView]$View, [string]$Path, [string]$Name, [string]$Value) {
    $Label = if ($Name) { $Name } else { '(default)' }
    Write-Host "  set HKCU\$Path$(Get-ViewLabel $View) [$Label] = $Value"
    if ($DryRun) { return }
    $Key = (Open-UserRoot $View).CreateSubKey($Path)
    try { $Key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String) } finally { $Key.Close() }
}

function Get-Value([Microsoft.Win32.RegistryView]$View, [string]$Path, [string]$Name) {
    $Key = (Open-UserRoot $View).OpenSubKey($Path)
    if (-not $Key) { return $null }
    try { return $Key.GetValue($Name) } finally { $Key.Close() }
}

function Remove-Tree([Microsoft.Win32.RegistryView]$View, [string]$Path) {
    Write-Host "  remove HKCU\$Path$(Get-ViewLabel $View)"
    if ($DryRun) { return }
    (Open-UserRoot $View).DeleteSubKeyTree($Path, $false)
}

function Remove-IfEmpty([Microsoft.Win32.RegistryView]$View, [string]$Path) {
    $Key = (Open-UserRoot $View).OpenSubKey($Path)
    if (-not $Key) { return }
    $Empty = $Key.SubKeyCount -eq 0 -and $Key.ValueCount -eq 0
    $Key.Close()
    if ($Empty) { Remove-Tree $View $Path }
}

function Test-InThisFolder([string]$DllPath) {
    if (-not $DllPath) { return $false }
    $Folder = Split-Path -Parent $DllPath
    return [string]::Equals($Folder.TrimEnd('\'), $Here.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)
}

function Send-AssociationChanged {
    if ($DryRun -or $TestRoot) { return }
    Add-Type -Namespace Solstice -Name Shell -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
'@
    # SHCNE_ASSOCCHANGED, SHCNF_IDLIST
    [Solstice.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

$ClsidPath = "$Base\Classes\CLSID\$ThumbnailClsid"
$Registry64 = [Microsoft.Win32.RegistryView]::Registry64

if (-not $Unregister) {
    foreach ($Entry in $Views) {
        if (-not (Test-Path -LiteralPath (Join-Path $Here $Entry.Dll))) { throw "Missing $($Entry.Dll) in $Here" }
    }
    $Previous = Get-Value $Registry64 "$ClsidPath\InprocServer32" ''
    if ($Previous -and -not (Test-InThisFolder $Previous)) {
        Write-Host "Replacing the current user's registration from: $Previous"
    }
    Write-Host 'Registering the .kra/.krz thumbnail provider for the current user:'
    foreach ($Entry in $Views) {
        $Dll = Join-Path $Here $Entry.Dll
        Set-Value $Entry.View $ClsidPath '' 'Krita Thumbnail Provider (Solstice)'
        Set-Value $Entry.View "$ClsidPath\InprocServer32" '' $Dll
        Set-Value $Entry.View "$ClsidPath\InprocServer32" 'ThreadingModel' 'Apartment'
    }
    foreach ($Extension in $Extensions) {
        $Path = "$Base\Classes\$Extension\shellex\$ThumbnailProviderIid"
        $Existing = Get-Value $Registry64 $Path ''
        if ($Existing -and $Existing -ne $ThumbnailClsid) {
            Write-Host "Replacing the current user's $Extension thumbnail provider $Existing"
        }
        Set-Value $Registry64 $Path '' $ThumbnailClsid
    }
    Set-Value $Registry64 $MarkerPath 'InstallLocation' $Here
    Set-Value $Registry64 $MarkerPath 'Version' '1.2.4d'
    Send-AssociationChanged
    if ($DryRun) { Write-Host 'Dry run: nothing was changed.' }
    else { Write-Host 'Done. New .kra/.krz thumbnails appear in Explorer; previously cached ones may need a refresh.' }
    exit 0
}

Write-Host 'Removing the current user''s .kra/.krz thumbnail provider registered from this folder:'
$Ours = Test-InThisFolder (Get-Value $Registry64 "$ClsidPath\InprocServer32" '')
if (-not $Ours) {
    $Current = Get-Value $Registry64 "$ClsidPath\InprocServer32" ''
    if ($Current) { Write-Host "  The current registration belongs to another folder ($Current); it is kept." }
    else { Write-Host '  Nothing is registered for the current user.' }
} else {
    foreach ($Entry in $Views) {
        if (Test-InThisFolder (Get-Value $Entry.View "$ClsidPath\InprocServer32" '')) {
            Remove-Tree $Entry.View $ClsidPath
        }
    }
    foreach ($Extension in $Extensions) {
        $Path = "$Base\Classes\$Extension\shellex\$ThumbnailProviderIid"
        if ((Get-Value $Registry64 $Path '') -eq $ThumbnailClsid) {
            Remove-Tree $Registry64 $Path
            Remove-IfEmpty $Registry64 "$Base\Classes\$Extension\shellex"
            Remove-IfEmpty $Registry64 "$Base\Classes\$Extension"
        }
    }
}
$MarkedFolder = Get-Value $Registry64 $MarkerPath 'InstallLocation'
if ($MarkedFolder -and (Test-InThisFolder (Join-Path $MarkedFolder 'x'))) {
    Remove-Tree $Registry64 $MarkerPath
    Remove-IfEmpty $Registry64 "$Base\Solstice"
}
Send-AssociationChanged
if ($DryRun) { Write-Host 'Dry run: nothing was changed.' }
else { Write-Host 'Done. Explorer may keep the DLL loaded until it restarts or you sign out.' }
