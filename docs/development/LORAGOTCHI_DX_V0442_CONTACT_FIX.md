# v0.4.4.2 M2 DX contact selector fix

MeshCore's BaseChatMesh reserves MAX_ANON_CONTACTS entries at the start of its
contact slot array. getNumContacts() excludes those reserved slots, while
getContactByIdx() indexes the underlying slot array.

The DX selector now offsets logical contact numbers by MAX_ANON_CONTACTS and
rejects empty names. Recall uses the same offset.

No CardPuter update is required.
