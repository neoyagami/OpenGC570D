# SPDX-License-Identifier: GPL-2.0-only

Name:           gc570d
Version:        1.0.6
Release:        1%{?dist}
Summary:        Integration files for the OpenGC570D capture-card driver
License:        GPL-2.0-only
URL:            https://github.com/neoyagami/OpenGC570D
Source0:        %{url}/archive/refs/tags/v%{version}/OpenGC570D-%{version}.tar.gz

BuildArch:      noarch
Provides:       %{name}-kmod-common = %{version}-%{release}
Requires:       %{name}-kmod >= %{version}
Recommends:     wireplumber

%description
Documentation and WirePlumber integration for the experimental OpenGC570D
Linux driver for the AVerMedia Live Gamer DUO GC570D PCIe capture card.

The matching kernel-module RPM is built separately for the exact distribution
kernel from the same released source archive.

%prep
%autosetup -n OpenGC570D-%{version}

%build
# No architecture-independent files need to be built.

%check
test -f config/wireplumber/51-gc570d.conf
grep -F 'AVerMedia Live Gamer DUO' config/wireplumber/51-gc570d.conf

%install
install -D -m 0644 config/wireplumber/51-gc570d.conf \
    %{buildroot}%{_datadir}/wireplumber/wireplumber.conf.d/51-gc570d.conf

%files
%license LICENSE
%doc README.md NOTICE.md MAINTAINERS.md SECURITY.md CONTRIBUTING.md
%doc docs/DATA_PROVENANCE.md docs/INSTALL.md docs/LEGAL.md
%doc docs/PACKAGING.md docs/REVERSE_ENGINEERING.md docs/STATUS.md docs/USAGE.md
%{_datadir}/wireplumber/wireplumber.conf.d/51-gc570d.conf

%changelog
* Mon Nov 02 2026 neoyagami <neoyagami@gmail.com> - 1.0.6-1
- Document validated 5.1 LPCM passthrough and resilient stereo capture
- Include standalone HDMI IN 1 and lower-resolution hardware capture support

* Sat Oct 24 2026 neoyagami <neoyagami@gmail.com> - 1.0.5-1
- Document validated 2160p60 SDR and HDR capture support
- Build portable installations with the distribution profile

* Fri Oct 23 2026 neoyagami <neoyagami@gmail.com> - 1.0.4-1
- Add the initial common package for distribution builds
- Install documentation and the system WirePlumber capture policy

* Fri Oct 23 2026 neoyagami <neoyagami@gmail.com> - 1.0.3-1
- Initial common package for OpenGC570D
- Install documentation and the system WirePlumber capture policy
