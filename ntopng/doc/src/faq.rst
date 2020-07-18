FAQ
###

Cannot Login into the GUI
=========================

If you locked yourself out of the ntopng GUI after a change in the `authentication method`_,
you can reset the ntopng login method to the default one (local authentication) with the following command:

.. code:: bash

  # For ntopng 3.7 and above:
  redis-cli set ntopng.prefs.local.auth_enabled 1

  # For ntopng 3.6 and below:
  redis-cli set ntopng.prefs.auth_type local

After restarting ntopng, you should be able to login with the administrator credentials.

If you forgot the administrator password, you can reset it with the following command:

.. code:: bash

  redis-cli del ntopng.user.admin.password

After restarting ntopng, you can login with the default ntopng credentials (admin, admin).

.. warning::

   Some browser plugins that perform javascript blocking may interfere with the login.
   Sometimes adding an exception for ntopng is not enough. Try to disable them before
   reporting an issue.

.. note::

   If the login issue persist, deleting the browser cookies sometimes helps.

.. _`authentication method`: advanced_features/authentication.html

Too many failed authentication attempts
=======================================

If a device tries to login with wrong credentials too many times, it's IP
address is blacklisted for 5 minutes. In order to manually clean the blacklist,
the following command can be used:

.. code:: bash

  for k in `redis-cli keys ntopng.cache.failed_logins.*`; do redis-cli del $k; done

After executing the above command and restarting ntopng, the device will be able to
login again.

How can I monitor the sites visited by an host?
===============================================

To monitor the web sites visited by an host, make sure it is local (if unsure
about what is a local host check page :ref:`Hosts`). Then, you'll be
able to immediately view its visited sites by following the simple
instructions available at :ref:`Sites`.

Note that both plain HTTP sites as well as HTTPS sites are monitored!

How do I know who is using most of my bandwidth?
================================================

Is your Internet connection slow? Do you think someone in the network
is consuming all the available bandwidth? Well, ntopng helps you in
understanding this.

First, have a look at the :ref:`Dashboard` and the
:ref:`DashboardPro`. You'll immediately see the top local talkers and
the top remote destinations there!

Then, you can click on any of the hosts that have the highest throughput to
see what are they doing. Select the :ref:`Flows` page and sort flows
according to an ascending order of the "Actual Thpt" to have
visibility on the actually traffic the host is currently doing.


How can I associate an hostname to a certain category?
======================================================

Let's say you want to want to associate traffic from and to hostname
:code:`crazynetwork.it` to category "Download-FileTransfer-FileSharing" as
you know that that particular host is a mirror for the download of ISO
images.

Select the "Settings" wheel and click on "Categories"

.. figure:: ./img/faq_edit_traffic_categories.png
  :align: center
  :alt: Edit Categories

  The Edit Categories Menu Entry

Then, add hostname :code:`crazynetwork.it` to category
"Download-FileTransfer-FileSharing" by clicking on "Edit Hosts".

.. figure:: ./img/faq_edit_traffic_categories_custom_host.png
  :align: center
  :alt: Edit Category Hostnames

  The Edit Category Hostnames Page

Finally, save and from that moment on you'll have the matching traffic
opportunely categorized

.. figure:: ./img/faq_edit_traffic_categories_flow.png
  :align: center
  :alt: Flow With a Custom Category

  The Flow Details Page of a Flow with a Custom Category

Additional details and examples can be found at :ref:`CustomCategoryHosts`.

How can I estimate the disk space required by ntopng?
=====================================================

The following blog post gives accurate estimations of the disk space
required by ntopng to store timeseries and flows:
https://www.ntop.org/ntopng/ntopng-disk-requirements-for-timeseries-and-flows/

Cannot see data in Grafana
==========================

When ntopng is configured to export timeseries to InfluxDB, it is possible to use
grafana with InfluxDB as a data source to create customized dashboard. If the grafana
visualization does display any data, here are some steps for the troubleshooting:

- Verify that the data is actually written to the database. An easy way to do this
  is to open the interface charts page into ntopng and see if the past traffic is shown.
  A more accurate way is to run a query from command line:
  `influx -database ntopng -execute 'select * from "iface:traffic" order by time desc limit 1'`
  it will show the most recent data point written into the DB.

- Ensure that the InfluxDB database connected to grafana is the same as the database
  configured into the ntopng timeseries settings.

- If the data is correctly written to the database, the problem may be related to
  the missing `Time Series Index` due to an InfluxDB version upgrade. The necessary
  steps to enable the TSI are described in the `InfluxDB documentation`_. In short, the
  steps to perform should be:

  1. Change the `[data]` section of the config and set `index-version = "tsi1"`
  2. Convert the existing data to the TSI:
     `su -m influxdb -c "influx_inspect buildtsi -database ntopng -datadir /var/lib/influxdb/data -waldir /var/lib/influxdb/wal"`

.. _`InfluxDB documentation`: https://docs.influxdata.com/influxdb/v1.7/administration/upgrading
