%define        __spec_install_post %{nil}
%define          debug_package %{nil}
%define        __os_install_post %{_dbpath}/brp-compress

Name:		osv-cli
Version:	1.0
Release:	1%{?dist}
Summary:	A standalon CLI for the OSv

Group:		Development/Tools
License:	FreeBSD
URL:		http://cloudius-systems.com/
Source0:	%{name}-%{version}.tar.gz

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires:	libedit
Requires:       openssl-libs
Requires:       ncurses
%description


%prep
%setup -q


%build


%install
rm -rf %{buildroot}
mkdir -p  %{buildroot}

# in builddir
cp -a * %{buildroot}


%post
echo "(cd %{_libdir}/osv-cli && exec" './cli $@)' > %{_bindir}/cli
chmod a+x %{_bindir}/cli

%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
%{_libdir}/*
%doc

%postun
rm %{_bindir}/cli


%changelog
