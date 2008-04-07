#ifndef CFGFILE_H_
#define CFGFILE_H_

struct cfg {
	float sensitivity;
	int dead_threshold;
	int invert[6];
};

void default_cfg(struct cfg *cfg);
int read_cfg(const char *fname, struct cfg *cfg);
int write_cfg(const char *fname, struct cfg *cfg);

#endif	/* CFGFILE_H_ */
