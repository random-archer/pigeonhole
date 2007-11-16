require ["fileinto", "reject", "envelope"];
require ["comparator-i;octet", "comparator-i;ascii-casemap"];

if anyof(exists "frop", size :over 45, size :under 10, address "from" "frop@student.utwente.nl") {
	keep;
} else {
	if envelope ["from", "cc"] "sirius@drunksnipers.com" {
		reject "I dont want your email.";
	} else {
		fileinto "INBOX.Wtf";
	}
}
