# Build this using apx-rpmbuild.
%define name elmlink

Name:           %{name}
Version:        %{version_rpm_spec_version}
Release:        %{version_rpm_spec_release}%{?dist}
Summary:        The APx ELM Link daemon

License:        Reserved
URL:            https://github.com/uwcms/APx-%{name}
Source0:        %{name}-%{version_rpm_spec_version}.tar.gz

#BuildRequires:  #
#Requires:       #

%global debug_package %{nil}

%description
A daemon serving as the bridge between the multiplexed IPMC<->ELM UART link and
client daemons.


%prep
%setup -q


%build
##configure
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
install -D -m 0755 elmlinkd %{buildroot}/%{_bindir}/elmlinkd
install -D -m 0755 elmlink-lowlevel-send %{buildroot}/%{_libexecdir}/elmlink-lowlevel-send
install -D -m 0644 elmlink.service %{buildroot}/%{_unitdir}/elmlink.service
install -D -m 0644 elmlink.conf %{buildroot}/%{_sysconfdir}/elmlink.conf
install -d -m 0755 %{buildroot}/%{_sysconfdir}/elmlink.permissions.d
install -D -m 0644 permissions.README %{buildroot}/%{_sysconfdir}/elmlink.permissions.d/README


%files
%{_bindir}/elmlinkd
%{_libexecdir}/elmlink-lowlevel-send
%{_unitdir}/elmlink.service
%config(noreplace) %{_sysconfdir}/elmlink.conf
%dir %{_sysconfdir}/elmlink.permissions.d
%{_sysconfdir}/elmlink.permissions.d/README


%post
%systemd_post elmlink.service


%preun
%systemd_preun elmlink.service


%postun
%systemd_postun_with_restart elmlink.service


%changelog
* Thu Mar 04 2021 Jesra Tikalsky <jtikalsky@hep.wisc.edu>
- Initial spec file
