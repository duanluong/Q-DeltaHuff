Write-Host "--- Checking format of the following files ..."

$needsFix = @()

foreach ($f in $AllFiles) {
    clang-format -n --fail-on-incomplete-format --Werror --style=file --ferror-limit=1 $f.FullName 2>&1

    if ($LASTEXITCODE -ne 0) {
        $needsFix += $f
    }
}

if ($needsFix.Count -eq 0) {
    Write-Host "All files are correctly formatted"
} else {
    Write-Host "The following files need formatting:"
    $needsFix | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "... ---"
