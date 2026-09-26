# Source provenance and third-party notices

The initial `core/`, `driver/`, `scripts/` and `tests/` implementation is original project source, identified by SPDX `GPL-3.0-only`. The existing GNU GPL version 3 LICENSE file is retained unchanged.

No Microsoft Windows-driver-samples implementation, ReactOS driver source, Linux driver source, Mesa source or third-party shader blob is copied into the current driver. Those projects are references, not bundled implementation dependencies. In particular, Microsoft's sample repository uses MS-PL; it is not silently relicensed under GPL by this project.

Microsoft Windows SDK/WDK packages, Visual Studio compiler tools and libraries are downloaded/used separately under their applicable Microsoft terms. GitHub Actions are invoked by pinned commit references and are not vendored as source. Generated binaries are experimental lab artifacts, not Microsoft-signed or certified products.

See `docs/SOURCE_AUDIT.md` for the initial dependency and reuse decisions. Any subsequent imported source must retain its original copyright/license notices and add an entry here before release. Do not invent a copyright attribution or assume all files in another repository share the same license.
