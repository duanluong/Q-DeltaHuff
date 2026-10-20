param(
    [Parameter(Mandatory = $true)]
    [string]$MainBranch
)

if ($MainBranch -eq 0) {
    Write-Host "No main branch is provided, all files will be searched."
    exit 1
}

Write-Host "--- Collecting Diff File List from branch" $MainBranch "..."

# Find the diff file list
$MergeBase = git merge-base $MainBranch HEAD
$DiffList = git diff --name-only $MergeBase HEAD

# Convert to absolute paths
$RepoRoot = (git rev-parse --show-toplevel).Trim()
$AbsoluteDiffList = $DiffList | ForEach-Object { Get-ChildItem -Path $_ -File -ErrorAction SilentlyContinue }

# Get the total file list
. .\tools\code_format\get_all_files.ps1

# Find the common files between the diff & total lists
$AllFilesFullName = $AllFiles.FullName
$MatchedList = $AbsoluteDiffList | Where-Object { $AllFilesFullName -contains $_.FullName }

# Assign back to the target of formating process
$AllFiles = $MatchedList

Write-Host "... ---"
