#define B 1
#define MAXRATE 125000000.0 

static double p_to_b(double p, double rtt, double tzero, int psize) 
{
	//equation from Padhye, Firoiu, Towsley and Kurose, Sigcomm 98
	//ignoring the term for rwin limiting
	double tmp1, tmp2, res;
	res=rtt*sqrt(2*B*p/3);
	tmp1=3*sqrt(3*B*p/8);
	if (tmp1>1.0) tmp1=1.0;
	tmp2=tzero*p*(1+32*p*p);
	res+=tmp1*tmp2;
	if (res==0.0) {
		res=MAXRATE;
	} else {
		res=psize/res;
	}
#ifdef DEBUF
	printf("p:%f rtt:%f tzero:%f result:%f\n", p, rtt, tzero, res);
#endif
	return res;
}
