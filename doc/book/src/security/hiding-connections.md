<!--
Copyright (c)  2018-2020  Janik Rabe

Permission is granted to copy, distribute and/or modify this document
under the terms of the GNU Free Documentation License, Version 1.3
or any later version published by the Free Software Foundation;
with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
A copy of the license is included in the file 'COPYING.DOC'
-->

# Hiding Connections

It is recommended to hide connections to any servers running on your machine to
avoid disclosing unnecessary information.

The recommended way to accomplish this is to hide all connections and only
respond to queries for certain user accounts:

```
default {
	default {
		force hide
	}
}

user "ryan" {
	default {
		force reply "ryan"
	}
}
```

It is also possible to hide individual users' connections:

```
user "root" {
	default {
		force hide
	}
}

user "http" {
	default {
		force hide
	}
}
```

Alternatively, the [`random`][cap_random] and
[`random_numeric`][cap_random_numeric] capabilities may be used to conceal
users' real login names while still allowing the system administrator to
identify who was responsible for a particular connection.
See the [list of capabilities][caps] for more information.

[caps]:               ../getting-started/capabilities.md
[cap_random]:         ../getting-started/capabilities.md#random
[cap_random_numeric]: ../getting-started/capabilities.md#random_numeric
