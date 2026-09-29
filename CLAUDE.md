# Start here for Starlight

Read WORKSPACE-STRUCTURE.md in this directory first, then Starlight Documents/Current/docs/claude-handoff/START-HERE.md.

This root is the only authoritative Starlight development checkout. editor and the live source directories under deep are one engine. Do not search analysis, publication, historical documentation or old automation results for a different engine version.

publication is an independent sanitized Git repository for GitHub; never upload the private parent history. analysis contains reports and historical evidence, not build instructions. New analysis reports belong under analysis/run-* and are excluded from engine inventory.

Current cleanup verification covers analyzer output isolation, analyzer unit checks and its GUI build. It does not close the two previously recorded runtime failures or certify a new full engine run. Cleanup still has explicitly listed deletion blockers; see CLEANUP-STATUS.md.

GameProject, Claude Code and Starbus are outside this cleanup. Do not change those projects or services as part of Starlight organization.
