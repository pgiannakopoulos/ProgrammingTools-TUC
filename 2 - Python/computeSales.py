def menu():
    choice = input("\tMENU \n 1: read new input file\n 2: print statistics for a specific product\n 3: print statistics for a specific AFM\n 4: exit the program\nGive your preference: ")

    if choice == "1":
        readfile()
    elif choice == "2":
        print_stats1()
    elif choice == "3":
        print_stats2()
    elif choice=="4":
        exit()
    else:
        menu()
    menu()

# def print_db(db):
#     for afm, product_list in db.items():
#         print("AFM: ", afm)
#         for product_name, price_list in product_list.items():
#             for price in price_list:
#                 print(product_name, price_list[price], price)
#         print("==========")

def check_receipt_splitters(line):
    for x in line:
        if x != '-' and x !=line[len(line)-1]:
            return False
    
    return True
        
def check_afm(afm):
    if len(afm)==10 and afm.isdigit()==True:
        return True
    else:
        return False

def check_product(product, product_list):
    #check if quantity is integer
    try: 
        product[1]=int(product[1])
    except ValueError:
        return False
    #check if unit price and sum cost are floats
    if (len(product[2].rsplit('.')[-1]) != 2) or (len(product[3].rsplit('.')[-1]) != 2): #old: '.' not in product[2]
        return False

    try:
        product[2]=float(product[2])
    except ValueError:
        return False

    try: 
        product[3]=float(product[3])
    except ValueError:
        return False

    if product[1]<0 or product[2]<0 or product[3]<0:
        return False

    #check if unit_price * quntity = sum_cost
    multiCheck = product[1]*product[2]
    if (product[3]-multiCheck>=0.009) or (product[3]-multiCheck<=-0.009):#it is not 0.01 because the deduction is not 100% accurate
       return False

    return True

def check_sum(product_list, totalSum):
    if len(totalSum.rsplit('.')[-1]) != 2:   #check if the sum has exactly 2 decimals
        return False
    try:
        totalSum=float(totalSum)
    except ValueError:
        return False

    checkSum=0.0
    for x in product_list:
        checkSum+=x[1]*x[2]

    checkSum = float("{0:.2f}".format(checkSum))  #keep only the last 2 decimals

    if (checkSum != totalSum):
        return False
    
    return True

def database_insert(database, afm, product):
    product_name = product[0]
    product_amount = product[1]
    product_unit_price = product[2]
        
    if afm in database:     #check if the afm is in database
        if product_name in database[afm]:   #check if the product is in database
            if product_unit_price in database[afm][product_name]:   #check if the current price is in database
                temp = database[afm][product_name][product_unit_price] + product_amount
                database[afm][product_name][product_unit_price] = temp
            else:
                database[afm][product_name][product_unit_price] = product_amount
        else:
            database[afm][product_name]  = {}
            database[afm][product_name][product_unit_price] = product_amount
    else:
        database[afm] = {}  #create new dictionary entry for new afm
        database[afm][product_name]  = {} #create new dictionary entry for new price
        database[afm][product_name][product_unit_price] = product_amount

def readfile():
    file = input("Enter the file name: ")
    
    try:
     	f = open(file,'r',encoding="utf-8") 
    except FileNotFoundError:
        print("File does not exists!")
        return

    line = f.readline()

    validator = True
    val_sum = False
    product_list = []  #declare a list

    while line:
        line = line.upper()   #capitilize
        if check_receipt_splitters(line)==True:       #new receipt
            if len(product_list) != 0 and validator == True and val_sum == True:
                for i in range(0,len(product_list)):  #save the previous receipt
                    database_insert(database, afm, product_list[i])
                validator = True #reset validator
                val_sum = False
            product_list.clear()
            line = f.readline()
            afm_line = line.split()
            try:
                if afm_line[0] != "ΑΦΜ:":
                    validator=False;
                else:
                    afm = afm_line[1]  #save afm
                    validator = check_afm(afm)
            except:
                validator = False
            
        elif ("ΣΥΝΟΛΟ" in line) and validator == True: #end of receipt
            totalSum = line.split()
            try:
                validator = check_sum(product_list, totalSum[1])
            except:
                validator = False
            
            if validator == True:
                val_sum = True 
        elif validator == True:
            product = line.split()  #save the product information in a list
            try:
                if len(product) != 4:
                    validator = False
                elif check_product(product, product_list) == False:
                    validator = False
                else:
                    product_name = product[0][0:(len(product[0])-1)] #ignore : after name
                    product_amount = int(product[1])
                    product_unit_price = float(product[2])
                    product_info = (product_name,product_amount,product_unit_price)
                    product_list.append(product_info)  #append all products of a receipt to a temp list
            except:
                validator == False

        line = f.readline()

def print_stats1():
    product_name = input("Enter the product name: ")
    product_name = product_name.upper()               #capitilize the name
    for afm in sorted(database.keys()):               #browse to all afms
        if product_name in database[afm]:             #check is the product is associated with the afm
            sum = 0
            for price in database[afm][product_name]:   #for each different product price
                partial_sum = database[afm][product_name][price] * price
                sum = sum + partial_sum 
            print(afm,"{:.2f}".format( sum )) 

def print_stats2():
    afm = input("Enter ΑΦΜ: ")
    if check_afm(afm)==False:
        print("Wrong ΑΦΜ; Inputs that are not 10 digits or contain characters are rejected.")

    if afm in database.keys():
        products = sorted(database[afm])
        for x in products:
            sum = 0
            for price in database[afm][x]:   #for each different product price
                partial_sum = database[afm][x][price] * price
                sum = sum + partial_sum 
            print(x,"{:.2f}".format( sum ))


database = {}  #declare a dictionary
menu()
