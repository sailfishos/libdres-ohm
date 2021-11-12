Name:       libdres

Summary:    Dependency resolver for OHM
Version:    1.1.17
Release:    1
License:    LGPLv2
URL:        https://git.sailfishos.org/mer-core/libdres-ohm
Source0:    %{name}-%{version}.tar.gz
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(libohmplugin) >= 1.2.0
BuildRequires:  pkgconfig(libohmfact) >= 1.2.0
BuildRequires:  pkgconfig(libprolog)
BuildRequires:  pkgconfig(libsimple-trace)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  bison
BuildRequires:  flex

%description
A dependency resolver for OHM.

%package utils
Summary:    Miscallaneous DRES utilities, including dresc
Requires:   %{name} = %{version}-%{release}

%description utils
Miscallaneous DRES utilities, including dresc.

%package -n ohm-plugin-resolver
Summary:    OHM dependency resolver plugin
Requires:   %{name} = %{version}-%{release}
Requires:   ohm

%description -n ohm-plugin-resolver
A dependency resolver plugin for OHM.

%package devel
Summary:    Development files for %{name}
Requires:   %{name} = %{version}-%{release}

%description devel
Development files for %{name}.


%prep
%setup -q -n %{name}-%{version}


%build
echo -n "%{version}" > .tarball-version
%autogen --disable-static
%configure --disable-static
make


%install
rm -rf %{buildroot}
%make_install
rm -f %{buildroot}/%{_libdir}/*.la
rm -f %{buildroot}/%{_libdir}/ohm/*.la

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license COPYING
%{_libdir}/*.so.*

%files utils
%defattr(-,root,root,-)
%{_bindir}/dresc

%files -n ohm-plugin-resolver
%defattr(-,root,root,-)
%{_libdir}/ohm/libohm_dres.so
%config %{_sysconfdir}/ohm/plugins.d/dres.ini

%files devel
%defattr(-,root,root,-)
%doc README COPYING INSTALL AUTHORS NEWS ChangeLog
%{_includedir}/dres
%{_libdir}/*.so
%{_libdir}/pkgconfig/*
