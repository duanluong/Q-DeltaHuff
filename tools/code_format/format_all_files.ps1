. .\tools\code_format\get_all_files.ps1

Write-Host "--- Formatting ---"

foreach ($File in $AllFiles) {
    clang-format -i --verbose $File.FullName
}

Write-Host "--- Done ---"
