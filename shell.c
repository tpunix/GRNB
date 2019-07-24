
#include "grnb.h"

///////////////////////////////////////////////////////////////////////////////

extern int adecl(cmd_start[]), adecl(cmd_end[]);
#define CMD_START  adecl(cmd_start)
#define CMD_END    adecl(cmd_end)

static int shell_run;

///////////////////////////////////////////////////////////////////////////////

CMD_DESC *find_cmd(char *name)
{
	CMD_DESC *cmd;
	int len, *p;

	len = strlen(name);
	for(p=CMD_START; p<CMD_END; p++){
		cmd = (CMD_DESC*)*p;
		if(strncmp(cmd->name, name, len)==0)
			return cmd;
	}

	return NULL;
}

int exec_cmd(int argc, char *argv[])
{
	CMD_DESC *cmd, **sub_cmd;
	int i, len;

	cmd = find_cmd(argv[0]);
	if(cmd==NULL){
		goto _not_found;
	}

	sub_cmd = cmd->sub_cmd;
	if(sub_cmd==NULL){
		return cmd->func(argc, argv);
	}

	len = strlen(argv[1]);
	i = 0;
	while(sub_cmd[i]){
		if(strncmp(sub_cmd[i]->name, argv[1], len)==0)
			return sub_cmd[i]->func(argc-1, &argv[1]);
		i += 1;
	}

_not_found:
	printk("Cound not found command '%s'.\n", argv[0]);
	printk("Please type 'help' for more infomations.\n");
	return -1;
}

///////////////////////////////////////////////////////////////////////////////

int parse_arg(char *str, char *argv[], char **rest_str)
{
	int argc, in_str, ch;

	argc = 0;
	*rest_str = 0;
	while(1){
		while(*str==' ' || *str=='\t') str++;
		if(*str==0)
			break;

		argv[argc] = str;

		in_str = 0;
		while(*str){
			if(*str=='"'){
				if(in_str==0)
					in_str = 1;
				else
					in_str = 0;
			}

			if(in_str==0 && (*str==' ' || *str=='\t' || *str==';'))
				break;

			str += 1;
		}

		ch = *str;
		*str = 0;
		str += 1;
		if(argv[argc][0])
			argc += 1;

		if(ch==0)
			break;

		if(ch==';'){
			*rest_str = str;
			break;
		}
	}

	return argc;
}


int exec_cmdstr(char *str)
{
	int argc, retv;
	char *argv[16], *rest_str;

	rest_str = str;
	while(rest_str){
		argc = parse_arg(rest_str, argv, &rest_str);
		if(argc){
			reset_scroll_lock(0);
			retv = exec_cmd(argc, argv);
			reset_scroll_lock(-1);
			if(retv!=0)
				break;
		}
	}

	return retv;
}

///////////////////////////////////////////////////////////////////////////////

int command_help(int argc, char *argv[])
{
	CMD_DESC *cmd;
	int *p;

	if(argc>1){
		cmd = find_cmd(argv[1]);
		if(cmd==NULL){
			printk("No help infomations about '%s'.\n", argv[1]);
			return 0;
		}
		printk("%s\n", cmd->help_msg);
		if(cmd->help_long){
			printk("%s", cmd->help_long);
		}

		return 0;
	}

	printk("Command list:\n");
	printk("----------------------------------------------------------------\n");
	for(p=CMD_START; p<CMD_END; p++){
		cmd = (CMD_DESC*)*p;
		printk("  %-8s  --  %s\n", cmd->name, cmd->help_msg);
	}
	printk("----------------------------------------------------------------\n");
	printk("Type 'help cmd' to get more infomations.\n");

	return 0;
}

DEFINE_CMD(help, command_help, "Show help message", NULL);


int command_exit(int argc, char *argv[])
{
	shell_run = 0;
	return 0;
}

DEFINE_CMD(exit, command_exit, "Exit GRNB Shell", NULL);


///////////////////////////////////////////////////////////////////////////////

static int shell_gets(char *buf, int len)
{
	int n, ch, ctrl;

	printk("grnb> ");
	n = 0;
	while(1){
		ch = console_getc();
		ctrl = ch>>8;
		ch &= 0xff;
		if(ctrl==0x48){
			// UP
		}

		if(ch=='\r' || ch=='\n')
			break;
		if(ch=='\b'){
			if(n>0){
				n -= 1;
				printk("\b \b");
			}
		}else if(ch>=0x20){
			printk("%c", ch);
			buf[n] = ch;
			n++;
			if(n==len)
				break;
		}
	}

	printk("\n");
	buf[n] = 0;
	return n;
}

int grnb_shell(void)
{
	int len;
	char cmdstr[256];

	console_setcursor(1);

	shell_run = 1;
	while(shell_run){
		len = shell_gets(cmdstr, 256);
		if(len==0)
			continue;
		exec_cmdstr(cmdstr);
	}

	console_setcursor(0);

	return 0;
}

///////////////////////////////////////////////////////////////////////////////

