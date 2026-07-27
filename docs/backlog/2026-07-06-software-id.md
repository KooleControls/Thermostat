# Software ID

**Closed — dropped (2026-07-27).** The thermostat is an open-source, standalone
product (as if bought from a third party), so it carries **no KC software ID**.

Decision: with open-source approved (`2026-07-27-open-source-public-repo.md`), we
stop treating the thermostat as part of KC's internal firmware-identity scheme.

- The SID was **never embedded** in the firmware, so nothing has to be removed
  there — version/system-info reports the git-tag-derived firmware version and
  carries no KC identifier, which is already the desired end state.
- The only place the SID appeared was the **release-artifact filename**
  (`…_28_KC_Thermostat`). That has been removed: artifacts now use standalone
  naming (`Thermostat-<version>-…`) with no KC identifiers. See
  `docs/superpowers/specs/2026-07-27-open-source-and-release-workflow-design.md`.

The reservation on the Development-space Software IDs page can stay as a record;
it simply isn't used by this product. Recorded on Jira RA2-442.
