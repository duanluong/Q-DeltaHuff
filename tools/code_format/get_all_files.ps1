# Config search paths
$TargetDir = @(
    ".\src\Application",
    ".\lib",
    ".\inc",
    ".\examples"
)

# Folders to ignore (add relative or absolute paths here)
# Examples:
# ".\src\Middleware\ThirdParty",
# ".\src\OS\Common\Legacy",
# ".\src\Product\Tests"
$IgnoreDir = @(
    
)

# File extensions to include
$FileExtensions = @("*.cpp", "*.c", "*.h")

Write-Host "--- Collecting All File List ..."

# Filter valid paths
$Existing = $TargetDir | Where-Object { Test-Path $_ }
if (-not $Existing) {
    Write-Error "No valid search paths found."
    exit 1
}

# Resolve ignore folder paths to full, normalized paths
$IgnoreFull = foreach ($d in $IgnoreDir) {
    if (Test-Path $d) {
        # Normalize to full path and trim trailing slashes
        (Resolve-Path $d).Path.TrimEnd('\', '/')
    }
}

# If we have ignore paths, build a single regex that matches any ignored root
# We escape regex chars in paths to avoid accidental regex tokens.
$IgnoreRegex = $null
if ($IgnoreFull.Count -gt 0) {
    $escaped = $IgnoreFull | ForEach-Object { [regex]::Escape($_) }
    # Begin-of-string ^ then any of the ignore roots; case-insensitive match later
    $IgnoreRegex = '^(' + ($escaped -join '|') + ')(\\|/)'
}

# Collect files
$files = foreach ($p in $Existing) {
    foreach ($ext in $FileExtensions) {
        Get-ChildItem -Path $p -Recurse -Filter $ext -File -ErrorAction SilentlyContinue
    }
}

# Optionally exclude files that live under any ignored folder
if ($IgnoreRegex) {
    $files = $files | Where-Object {
        $fn = $_.FullName.TrimEnd('\', '/')
        # Keep if NOT under any ignored folder (case-insensitive)
        ($fn -notmatch $IgnoreRegex)
    }
}

# Merge + dedupe
$AllFiles = $files | Sort-Object FullName -Unique

Write-Host "... ---"
