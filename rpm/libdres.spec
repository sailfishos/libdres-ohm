Name:       libdres

Summary:    Dependency resolver for OHM
Version:    1.1.19
Release:    1
License:    LGPLv2
URL:        https://github.com/sailfishos/libdres-ohm
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
%make_build


%install
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%license COPYING
%{_libdir}/*.so.*

%files utils
%{_bindir}/dresc

%files -n ohm-plugin-resolver
%{_libdir}/ohm/libohm_dres.so
%config %{_sysconfdir}/ohm/plugins.d/dres.ini

%files devel
%doc README COPYING INSTALL AUTHORS NEWS ChangeLog
%{_includedir}/dres
%{_libdir}/*.so
%{_libdir}/pkgconfig/*
