
all-local:
	@for f in ohm-plugin src; do \
	    case $$f in  \
	    src) d=@PACKAGE@ ;; \
	    *)   d=$$f ;; \
	    esac ; \
	    mkdir -p $$d ; \
	    echo "===== building doc's for '$$d' ====="; \
	    if [ -d $$d/lyx ] ; then \
	        pushd $$d/lyx ; \
	        rm -f *.xml ; \
	        lyxfiles=`echo *.lyx` ;\
		for lf in $$lyxfiles ; do \
	            echo "export $$lf"; \
	            $(PD_LYX) --export docbook-xml $$lf ; \
	        done ; \
	        figfiles=`echo *.fig` ;\
	        if [ $$figfiles != "*.fig" ] ; then \
	            for fig in $$figfiles ; do \
	                pdf=$${fig/.fig/.pdf} ; \
	                png=$${fig/.fig/.png} ; \
	                rm -f $$pdf $$png ; \
	                echo "export $$fig to pdf and png"; \
	                $(PD_FIG2DEV) -L pdf -m 0.75 $$fig $$pdf ; \
	                $(PD_FIG2DEV) -L png $$fig $$png ; \
	            done ; \
	        fi ; \
	        popd ;\
	    fi ; \
	    doxydir=$$d/doxygen ; \
	    doxmldir=$$doxydir/xml ; \
	    echo mkdir -p $$doxmldir ; \
	    mkdir -p $$doxmldir ; \
	    TOPDIR=$(abs_top_srcdir) SRCDIR=$$f MODULEDIR=$$d \
	       $(PD_DOXYGEN) Doxyfile ; \
	    dxfiles=`cd $$doxmldir; echo *_8c.xml`; \
	    for df in $$dxfiles ; do \
	        if [ -f $$doxmldir/$$df ] ; then \
	            module=`echo $$df | sed -e s/_8c.xml//`; \
	            echo "xsltproc $$module"; \
	            $(PD_XSLTPROC) $(PD_DOXML2DB_STY) $$doxmldir/$$df \
	                > $${doxydir}/$${d}_$${module}.xml ; \
	        fi ; \
	    done ; \
	    rm -rf $$doxmldir; \
	done
