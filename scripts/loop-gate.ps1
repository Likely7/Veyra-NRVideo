[CmdletBinding()]
param(
    [ValidateSet("preflight", "phase0", "phase1", "phase2", "phase3", "phase4", "phase5", "phase6", "phase7")]
    [string]$Gate = "preflight"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$checks = New-Object System.Collections.Generic.List[object]
$state = $null

function Add-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Detail
    )

    $checks.Add([pscustomobject]@{
        Check = $Name
        Passed = $Passed
        Detail = $Detail
    })
}

function Get-ProjectSourceSnapshot {
    $paths = @()
    $gitMarker = Join-Path $root ".git"

    if (Test-Path -LiteralPath $gitMarker) {
        $paths = @(& git -C $root ls-files --cached --others --exclude-standard)
        if ($LASTEXITCODE -ne 0) {
            throw "git ls-files failed while creating the gate mutation snapshot"
        }
    }
    else {
        $paths = @(
            "README.md",
            ".gitignore",
            "AGENTS.md",
            "VEYRA_PRODUCT_SPEC_V1.md",
            "VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md",
            "docs/COMPETITOR_AUDIT_2026-09-03.md",
            "docs/history/loop/LOOP_ENGINE.md",
            "docs/history/loop/REVIEW_PROMPT.md",
            "docs/history/loop/CONTROL_HASHES.json",
            "docs/history/loop/STATE.json",
            "docs/history/loop/BACKLOG.md",
            "docs/history/loop/JOURNAL.md",
            "docs/history/loop/EVIDENCE.md",
            "docs/history/loop/INBOX.md",
            "docs/WORKLOG.md",
            "scripts/loop-gate.ps1",
            "scripts/gates/README.md"
        )
    }

    $snapshot = @{}
    foreach ($relativePath in @($paths | Sort-Object -Unique)) {
        $fullPath = Join-Path $root $relativePath
        $snapshot[[string]$relativePath] = if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToUpperInvariant()
        }
        else {
            "<missing>"
        }
    }
    return $snapshot
}

function Compare-ProjectSourceSnapshot {
    param(
        [hashtable]$Before,
        [hashtable]$After
    )

    $allPaths = @(@($Before.Keys) + @($After.Keys) | Sort-Object -Unique)
    foreach ($relativePath in $allPaths) {
        $beforeHash = if ($Before.ContainsKey($relativePath)) { [string]$Before[$relativePath] } else { "<absent>" }
        $afterHash = if ($After.ContainsKey($relativePath)) { [string]$After[$relativePath] } else { "<absent>" }
        if ($beforeHash -ne $afterHash) {
            [string]$relativePath
        }
    }
}

function Require-File {
    param([string]$RelativePath)

    $path = Join-Path $root $RelativePath
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    Add-Check "file:$RelativePath" $exists $(if ($exists) { "present" } else { "missing" })
    return $exists
}

function Check-Binary {
    param(
        [string]$RelativePath,
        [long]$ExpectedSize,
        [string]$ExpectedSha256,
        [string]$ExpectedSignature,
        [string]$ExpectedSignerFragment = ""
    )

    $path = Join-Path $root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Check "binary:$RelativePath" $false "missing"
        return
    }

    $item = Get-Item -LiteralPath $path
    Add-Check "size:$RelativePath" ($item.Length -eq $ExpectedSize) "actual=$($item.Length) expected=$ExpectedSize"

    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
    Add-Check "sha256:$RelativePath" ($actualHash -eq $ExpectedSha256) "actual=$actualHash"

    $signature = Get-AuthenticodeSignature -LiteralPath $path
    $signatureText = [string]$signature.Status
    Add-Check "signature:$RelativePath" ($signatureText -eq $ExpectedSignature) "actual=$signatureText expected=$ExpectedSignature"

    if ($ExpectedSignerFragment.Length -gt 0) {
        $subject = if ($null -ne $signature.SignerCertificate) { [string]$signature.SignerCertificate.Subject } else { "" }
        Add-Check "signer:$RelativePath" ($subject -like "*$ExpectedSignerFragment*") "subject=$subject"
    }
}

$stopPath = Join-Path $root "loop\STOP"
if (Test-Path -LiteralPath $stopPath) {
    Write-Host "VEYRA LOOP PAUSED: loop\STOP exists. Persist state and exit; do not delete it."
    exit 20
}

$requiredFiles = @(
    "README.md",
    "AGENTS.md",
    "VEYRA_PRODUCT_SPEC_V1.md",
    "VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md",
    "docs/COMPETITOR_AUDIT_2026-09-03.md",
    ".gitignore",
    "docs\history\loop\LOOP_ENGINE.md",
    "docs\history\loop\REVIEW_PROMPT.md",
    "docs\history\loop\CONTROL_HASHES.json",
    "docs\history\loop\STATE.json",
    "docs\history\loop\BACKLOG.md",
    "docs\history\loop\JOURNAL.md",
    "docs\history\loop\EVIDENCE.md",
    "docs\history\loop\INBOX.md",
    "scripts\gates\README.md"
)

foreach ($requiredFile in $requiredFiles) {
    [void](Require-File $requiredFile)
}

$controlManifestPath = Join-Path $root "docs\history\loop\CONTROL_HASHES.json"
$expectedControlManifestHash = "547D8E6E11BF22FF44F88767FB0BF1A8410A70E7846681F4C63E7D998D9EB74C"
if (Test-Path -LiteralPath $controlManifestPath -PathType Leaf) {
    $actualControlManifestHash = (Get-FileHash -LiteralPath $controlManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $manifestHashValid = $actualControlManifestHash -eq $expectedControlManifestHash
    Add-Check "control-manifest-hash" $manifestHashValid "actual=$actualControlManifestHash"

    if ($manifestHashValid) {
        try {
            $controlManifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $controlManifestPath | ConvertFrom-Json
            $manifestEntries = @($controlManifest.files)
            $expectedControlPaths = @(
                ".gitignore",
                "AGENTS.md",
                "README.md",
                "VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md",
                "VEYRA_PRODUCT_SPEC_V1.md",
                "docs/COMPETITOR_AUDIT_2026-09-03.md",
                "docs/ACTIVE_DELIVERY_PLAN.md",
                "docs/history/loop/LOOP_ENGINE.md",
                "docs/history/loop/REVIEW_PROMPT.md",
                "scripts/gates/README.md"
            ) | Sort-Object
            $actualControlPaths = @($manifestEntries | ForEach-Object { [string]$_.path } | Sort-Object)
            $validManifestEntries = @($manifestEntries | Where-Object {
                $entryPath = [string]$_.path
                (-not [string]::IsNullOrWhiteSpace($entryPath)) -and
                    (-not [IO.Path]::IsPathRooted($entryPath)) -and
                    ($entryPath -notmatch '(^|[\\/])\.\.([\\/]|$)') -and
                    ([string]$_.sha256 -match '^[0-9A-Fa-f]{64}$')
            }).Count -eq $manifestEntries.Count
            $manifestSchemaValid = ($controlManifest.schemaVersion -eq 1) -and
                ($manifestEntries.Count -eq 10) -and
                $validManifestEntries -and
                (($actualControlPaths -join "|") -ceq ($expectedControlPaths -join "|"))
            Add-Check "control-manifest-schema" $manifestSchemaValid "schemaVersion=$($controlManifest.schemaVersion) files=$($manifestEntries.Count) exactProtectedSet=$((($actualControlPaths -join '|') -ceq ($expectedControlPaths -join '|')))"

            foreach ($entry in $manifestEntries) {
                $relativeControlPath = [string]$entry.path
                if ([string]::IsNullOrWhiteSpace($relativeControlPath) -or
                    [IO.Path]::IsPathRooted($relativeControlPath) -or
                    $relativeControlPath -match '(^|[\\/])\.\.([\\/]|$)') {
                    Add-Check "control:$relativeControlPath" $false "invalid relative path"
                    continue
                }

                $controlPath = Join-Path $root $relativeControlPath
                if (-not (Test-Path -LiteralPath $controlPath -PathType Leaf)) {
                    Add-Check "control:$($entry.path)" $false "missing"
                    continue
                }

                $controlHash = (Get-FileHash -LiteralPath $controlPath -Algorithm SHA256).Hash.ToUpperInvariant()
                $expectedControlHash = ([string]$entry.sha256).ToUpperInvariant()
                Add-Check "control:$($entry.path)" ($controlHash -eq $expectedControlHash) "actual=$controlHash"
            }
        }
        catch {
            Add-Check "control-manifest-schema" $false $_.Exception.Message
        }
    }
}

$statePath = Join-Path $root "docs\history\loop\STATE.json"
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    try {
        $state = Get-Content -Raw -Encoding UTF8 -LiteralPath $statePath | ConvertFrom-Json
        $validSchema = ($state.schemaVersion -eq 1) -and ($state.engineVersion -eq "veyra-loop-v1")
        Add-Check "state-json" $validSchema "schemaVersion=$($state.schemaVersion) engineVersion=$($state.engineVersion)"

        $allowedStatuses = @("ready", "running", "repairing", "needs_review", "paused", "blocked", "needs_handoff", "release_candidate", "distribution_blocked", "complete")
        $validStatus = $allowedStatuses -contains [string]$state.status
        Add-Check "state-status" $validStatus "status=$($state.status)"

        $phaseId = [int]$state.phase.id
        $allowedPhaseStates = @("not_started", "in_progress", "gate_passed", "review_passed", "passed", "blocked")
        $validCurrentPhase = ($phaseId -ge 0) -and ($phaseId -le 7) -and ($allowedPhaseStates -contains [string]$state.phase.state)
        Add-Check "state-current-phase" $validCurrentPhase "id=$phaseId state=$($state.phase.state)"

        $phaseRecords = @($state.phases)
        $phaseIds = @($phaseRecords | ForEach-Object { [int]$_.id } | Sort-Object -Unique)
        $allowedLedgerStates = @("not_started", "locked", "in_progress", "gate_passed", "review_passed", "passed", "blocked")
        $invalidLedgerStates = @($phaseRecords | Where-Object { $allowedLedgerStates -notcontains [string]$_.status })
        $validLedger = ($phaseRecords.Count -eq 8) -and (($phaseIds -join ",") -eq "0,1,2,3,4,5,6,7") -and ($invalidLedgerStates.Count -eq 0)
        Add-Check "state-phase-ledger" $validLedger "records=$($phaseRecords.Count) ids=$($phaseIds -join ',')"

        $currentLedgerRecords = @($phaseRecords | Where-Object { [int]$_.id -eq $phaseId })
        $phaseIdentityValid = ($currentLedgerRecords.Count -eq 1) -and
            ([string]$state.phase.gate -ceq "phase$phaseId") -and
            ([string]$state.phase.name -ceq [string]$currentLedgerRecords[0].name)
        Add-Check "state-phase-identity" $phaseIdentityValid "id=$phaseId gate=$($state.phase.gate) name=$($state.phase.name)"

        $phaseEvidenceValid = $true
        foreach ($record in $phaseRecords) {
            $recordStatus = [string]$record.status
            $hasGateEvidence = -not [string]::IsNullOrWhiteSpace([string]$record.gateEvidence)
            $hasReviewEvidence = -not [string]::IsNullOrWhiteSpace([string]$record.reviewEvidence)
            $hasCommit = -not [string]::IsNullOrWhiteSpace([string]$record.commit)

            if ((@("gate_passed", "review_passed", "passed") -contains $recordStatus) -and -not $hasGateEvidence) {
                $phaseEvidenceValid = $false
            }
            if ((@("review_passed", "passed") -contains $recordStatus) -and -not $hasReviewEvidence) {
                $phaseEvidenceValid = $false
            }
            if (($recordStatus -eq "passed") -and -not $hasCommit) {
                $phaseEvidenceValid = $false
            }
            if (($recordStatus -eq "locked") -and ($hasGateEvidence -or $hasReviewEvidence -or $hasCommit)) {
                $phaseEvidenceValid = $false
            }
        }
        Add-Check "state-phase-evidence" $phaseEvidenceValid "gate/review/commit fields must match ledger status"

        $completedCycles = [int]$state.cycle.completed
        $maximumCycles = [int]$state.cycle.maximum
        $sameFailureAttempts = [int]$state.cycle.sameFailureAttempts
        $maximumSameFailureAttempts = [int]$state.cycle.maximumSameFailureAttempts
        $noProgressCycles = [int]$state.cycle.noProgressCycles
        $maximumNoProgressCycles = [int]$state.cycle.maximumNoProgressCycles

        $validLimits = ($completedCycles -ge 0) -and
            ($completedCycles -le $maximumCycles) -and
            ($sameFailureAttempts -ge 0) -and
            ($sameFailureAttempts -le $maximumSameFailureAttempts) -and
            ($noProgressCycles -ge 0) -and
            ($noProgressCycles -le $maximumNoProgressCycles) -and
            ([int]$state.cycle.maximum -eq 120) -and
            ([int]$state.cycle.maximumSameFailureAttempts -eq 3) -and
            ([int]$state.cycle.maximumNoProgressCycles -eq 5)
        Add-Check "state-loop-limits" $validLimits "completed=$completedCycles/$maximumCycles sameFailure=$sameFailureAttempts/$maximumSameFailureAttempts noProgress=$noProgressCycles/$maximumNoProgressCycles"

        $validPhaseSequence = $true
        foreach ($record in $phaseRecords) {
            $recordId = [int]$record.id
            if ($recordId -lt $phaseId -and [string]$record.status -ne "passed") {
                $validPhaseSequence = $false
            }
            elseif ($recordId -eq $phaseId -and [string]$record.status -ne [string]$state.phase.state) {
                $validPhaseSequence = $false
            }
            elseif ($recordId -gt $phaseId -and [string]$record.status -ne "locked") {
                $validPhaseSequence = $false
            }
        }
        Add-Check "state-phase-sequence" $validPhaseSequence "current=$phaseId; earlier must be passed, later must be locked"

        $stateStatus = [string]$state.status
        $cycleBoundValid = ($completedCycles -lt $maximumCycles) -or
            (@("needs_handoff", "paused", "release_candidate", "distribution_blocked", "complete") -contains $stateStatus)
        $failureBoundValid = ($sameFailureAttempts -lt $maximumSameFailureAttempts) -or
            (@("blocked", "needs_handoff", "paused", "release_candidate", "distribution_blocked", "complete") -contains $stateStatus)
        $noProgressBoundValid = ($noProgressCycles -lt $maximumNoProgressCycles) -or
            (@("blocked", "needs_handoff", "paused", "release_candidate", "distribution_blocked", "complete") -contains $stateStatus)
        $loopBoundsValid = $cycleBoundValid -and $failureBoundValid -and $noProgressBoundValid
        Add-Check "state-loop-bounds" $loopBoundsValid "status=$stateStatus cycle=$cycleBoundValid failure=$failureBoundValid noProgress=$noProgressBoundValid"

        $allPhasesPassed = @($phaseRecords | Where-Object { [string]$_.status -eq "passed" }).Count -eq 8
        $completionHasGitPointers = (Test-Path -LiteralPath (Join-Path $root ".git")) -and
            (-not [string]::IsNullOrWhiteSpace([string]$state.baselineCommit)) -and
            (-not [string]::IsNullOrWhiteSpace([string]$state.lastGoodCommit))
        $completionConsistent = ([string]$state.status -ne "complete") -or
            ($allPhasesPassed -and
                (@($state.openP0P1Findings).Count -eq 0) -and
                (@($state.blockers).Count -eq 0) -and
                $completionHasGitPointers)
        Add-Check "state-completion" $completionConsistent "status=$($state.status) allPhasesPassed=$allPhasesPassed openP0P1=$(@($state.openP0P1Findings).Count) blockers=$(@($state.blockers).Count) gitPointers=$completionHasGitPointers"

        $releaseLike = @("release_candidate", "distribution_blocked") -contains [string]$state.status
        $releaseCandidateConsistent = (-not $releaseLike) -or
            ($allPhasesPassed -and
                (@($state.openP0P1Findings).Count -eq 0) -and
                $completionHasGitPointers)
        Add-Check "state-release-candidate" $releaseCandidateConsistent "status=$($state.status) allPhasesPassed=$allPhasesPassed openP0P1=$(@($state.openP0P1Findings).Count) gitPointers=$completionHasGitPointers"

        $distributionBlockedConsistent = ([string]$state.status -ne "distribution_blocked") -or
            ($releaseCandidateConsistent -and (@($state.blockers).Count -gt 0))
        Add-Check "state-distribution-blocked" $distributionBlockedConsistent "status=$($state.status) blockers=$(@($state.blockers).Count)"

        $blockedStateConsistent = ([string]$state.status -ne "blocked") -or
            (([string]$state.phase.state -eq "blocked") -and (@($state.blockers).Count -gt 0))
        Add-Check "state-blocked-consistency" $blockedStateConsistent "status=$($state.status) phaseState=$($state.phase.state) blockers=$(@($state.blockers).Count)"

        $hasNextAction = -not [string]::IsNullOrWhiteSpace([string]$state.nextAction)
        Add-Check "state-next-action" $hasNextAction $([string]$state.nextAction)
    }
    catch {
        Add-Check "state-json" $false $_.Exception.Message
    }
}

Check-Binary "nvngx_dlssnr.dll" 165840496 "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E" "Valid" "NVIDIA"
Check-Binary "renodx-dlss5-1.addon64" 359424 "837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F" "NotSigned"

$ignorePath = Join-Path $root ".gitignore"
if (Test-Path -LiteralPath $ignorePath -PathType Leaf) {
    $ignoreText = Get-Content -Raw -LiteralPath $ignorePath
    $requiredIgnoreEntries = @(
        "/nvngx_dlssnr.dll",
        "/renodx-dlss5-1.addon64",
        "/runtime_local/",
        "/third_party_local/",
        "/reference_local/",
        "/captures/",
        "/logs/",
        "/loop/STOP"
    )

    foreach ($entry in $requiredIgnoreEntries) {
        Add-Check "gitignore:$entry" ($ignoreText.Contains($entry)) $(if ($ignoreText.Contains($entry)) { "present" } else { "missing" })
    }
}

$gitDirectory = Join-Path $root ".git"
if (Test-Path -LiteralPath $gitDirectory) {
    $insideWorktreeOutput = @(& git -C $root rev-parse --is-inside-work-tree 2>$null)
    $insideWorktreeExit = $LASTEXITCODE
    $validGitRepository = ($insideWorktreeExit -eq 0) -and ($insideWorktreeOutput.Count -gt 0) -and ([string]$insideWorktreeOutput[-1] -eq "true")
    Add-Check "git-repository" $validGitRepository "rev-parse exitCode=$insideWorktreeExit inside=$($insideWorktreeOutput -join ',')"

    $gitStateLinksValid = $null -ne $state
    $gitStateLinkProblems = New-Object System.Collections.Generic.List[string]
    if ($null -eq $state) {
        $gitStateLinkProblems.Add("STATE unavailable")
    }
    else {
        $commitsToVerify = New-Object System.Collections.Generic.List[object]
        $baselineCommit = [string]$state.baselineCommit
        if ([string]::IsNullOrWhiteSpace($baselineCommit)) {
            $gitStateLinksValid = $false
            $gitStateLinkProblems.Add("baselineCommit missing")
        }
        else {
            $commitsToVerify.Add([pscustomobject]@{ Label = "baselineCommit"; Commit = $baselineCommit })
        }

        $lastGoodCommit = [string]$state.lastGoodCommit
        if (-not [string]::IsNullOrWhiteSpace($lastGoodCommit)) {
            $commitsToVerify.Add([pscustomobject]@{ Label = "lastGoodCommit"; Commit = $lastGoodCommit })
        }

        foreach ($record in @($state.phases | Where-Object { [string]$_.status -eq "passed" })) {
            $recordCommit = [string]$record.commit
            if (-not [string]::IsNullOrWhiteSpace($recordCommit)) {
                $commitsToVerify.Add([pscustomobject]@{ Label = "phase$($record.id).commit"; Commit = $recordCommit })
            }
        }

        foreach ($commitLink in $commitsToVerify) {
            & git -C $root cat-file -e "$($commitLink.Commit)^{commit}" 2>$null
            $commitExists = $LASTEXITCODE -eq 0
            if (-not $commitExists) {
                $gitStateLinksValid = $false
                $gitStateLinkProblems.Add("$($commitLink.Label) missing:$($commitLink.Commit)")
                continue
            }

            & git -C $root merge-base --is-ancestor $commitLink.Commit HEAD 2>$null
            if ($LASTEXITCODE -ne 0) {
                $gitStateLinksValid = $false
                $gitStateLinkProblems.Add("$($commitLink.Label) not ancestor of HEAD:$($commitLink.Commit)")
            }
        }
    }
    Add-Check "state-git-links" ([bool]$gitStateLinksValid) $(if ($gitStateLinkProblems.Count -eq 0) { "all referenced commits exist and are HEAD ancestors" } else { $gitStateLinkProblems -join "; " })

    $trackedSensitive = @(& git -C $root ls-files -- "nvngx_dlssnr.dll" "renodx-dlss5-1.addon64" "runtime_local" "third_party_local" "reference_local" "captures" "logs")
    Add-Check "git-sensitive-files" ($trackedSensitive.Count -eq 0) $(if ($trackedSensitive.Count -eq 0) { "none tracked" } else { $trackedSensitive -join ", " })

    $effectiveIgnoreTargets = @(
        "nvngx_dlssnr.dll",
        "renodx-dlss5-1.addon64",
        "runtime_local/.veyra-ignore-probe",
        "third_party_local/.veyra-ignore-probe",
        "reference_local/.veyra-ignore-probe",
        "captures/.veyra-ignore-probe",
        "logs/.veyra-ignore-probe",
        "loop/STOP"
    )
    foreach ($target in $effectiveIgnoreTargets) {
        & git -C $root check-ignore -q --no-index -- $target
        $ignoreExit = $LASTEXITCODE
        Add-Check "git-effective-ignore:$target" ($ignoreExit -eq 0) "git check-ignore exitCode=$ignoreExit"
    }

    & git -C $root diff --check
    $worktreeDiffExit = $LASTEXITCODE
    Add-Check "git-diff-check" ($worktreeDiffExit -eq 0) "exitCode=$worktreeDiffExit"

    & git -C $root diff --cached --check
    $stagedDiffExit = $LASTEXITCODE
    Add-Check "git-cached-diff-check" ($stagedDiffExit -eq 0) "exitCode=$stagedDiffExit"
}
else {
    Add-Check "git-repository" ($Gate -eq "preflight") "not initialized; allowed only for preflight/P0.2"
}

if ($Gate -ne "preflight") {
    if ($null -eq $state) {
        Add-Check "phase-gate-current-state" $false "STATE could not be loaded"
    }
    else {
        $requestedPhase = [int]$Gate.Substring(5)
        Add-Check "phase-gate-current-state" ($requestedPhase -eq [int]$state.phase.id) "requested=$requestedPhase current=$($state.phase.id)"
    }

    $gateScript = Join-Path $root ("scripts\gates\" + $Gate + ".ps1")
    if (-not (Test-Path -LiteralPath $gateScript -PathType Leaf)) {
        Add-Check "phase-gate:$Gate" $false "missing $gateScript; create a deterministic gate from the Playbook before claiming this phase"
    }
    else {
        $sourceSnapshotBefore = Get-ProjectSourceSnapshot
        $shell = (Get-Process -Id $PID).Path
        & $shell -NoProfile -ExecutionPolicy Bypass -File $gateScript -Root $root
        $gateExit = $LASTEXITCODE
        Add-Check "phase-gate:$Gate" ($gateExit -eq 0) "exitCode=$gateExit script=$gateScript"

        $sourceSnapshotAfter = Get-ProjectSourceSnapshot
        $mutatedProjectFiles = @(Compare-ProjectSourceSnapshot -Before $sourceSnapshotBefore -After $sourceSnapshotAfter)
        Add-Check "phase-gate-no-project-mutation" ($mutatedProjectFiles.Count -eq 0) $(if ($mutatedProjectFiles.Count -eq 0) { "non-ignored project files unchanged" } else { "changed=" + ($mutatedProjectFiles -join ",") })

        $stopCreatedByGate = Test-Path -LiteralPath $stopPath
        Add-Check "phase-gate-no-stop-creation" (-not $stopCreatedByGate) $(if ($stopCreatedByGate) { "loop/STOP appeared while gate ran" } else { "not created" })
    }
}

$checks | Format-Table -AutoSize -Wrap
$failed = @($checks | Where-Object { -not $_.Passed })

if ($failed.Count -gt 0) {
    Write-Host "VEYRA GATE FAILED: $($failed.Count) check(s) failed."
    exit 1
}

Write-Host "VEYRA GATE PASSED: $Gate ($($checks.Count) checks)"
exit 0
