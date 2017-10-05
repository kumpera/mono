//
// This file was generated by resx2sr tool
//

partial class SR
{
	public const string InternalCompilerError = "An unexpected exception occurred while binding a dynamic operation";
	public const string BindRequireArguments = "Cannot bind call with no calling object";
	public const string BindCallFailedOverloadResolution = "Overload resolution failed";
	public const string BindBinaryOperatorRequireTwoArguments = "Binary operators must be invoked with two arguments";
	public const string BindUnaryOperatorRequireOneArgument = "Unary operators must be invoked with one argument";
	public const string BindPropertyFailedMethodGroup = "The name '{0}' is bound to a method and cannot be used like a property";
	public const string BindPropertyFailedEvent = "The event '{0}' can only appear on the left hand side of +";
	public const string BindInvokeFailedNonDelegate = "Cannot invoke a non-delegate type";
	public const string BindBinaryAssignmentRequireTwoArguments = "Binary operators cannot be invoked with one argument";
	public const string BindBinaryAssignmentFailedNullReference = "Cannot perform member assignment on a null reference";
	public const string NullReferenceOnMemberException = "Cannot perform runtime binding on a null reference";
	public const string BindCallToConditionalMethod = "Cannot dynamically invoke method '{0}' because it has a Conditional attribute";
	public const string BindToVoidMethodButExpectResult = "Cannot implicitly convert type 'void' to 'object'";
	public const string BadBinaryOps = "Operator '{0}' cannot be applied to operands of type '{1}' and '{2}'";
	public const string BadIndexLHS = "Cannot apply indexing with [] to an expression of type '{0}'";
	public const string BadIndexCount = "Wrong number of indices inside []; expected '{0}'";
	public const string BadUnaryOp = "Operator '{0}' cannot be applied to operand of type '{1}'";
	public const string NoImplicitConv = "Cannot implicitly convert type '{0}' to '{1}'";
	public const string NoExplicitConv = "Cannot convert type '{0}' to '{1}'";
	public const string ConstOutOfRange = "Constant value '{0}' cannot be converted to a '{1}'";
	public const string AmbigBinaryOps = "Operator '{0}' is ambiguous on operands of type '{1}' and '{2}'";
	public const string AmbigUnaryOp = "Operator '{0}' is ambiguous on an operand of type '{1}'";
	public const string ValueCantBeNull = "Cannot convert null to '{0}' because it is a non-nullable value type";
	public const string WrongNestedThis = "Cannot access a non-static member of outer type '{0}' via nested type '{1}'";
	public const string NoSuchMember = "'{0}' does not contain a definition for '{1}'";
	public const string ObjectRequired = "An object reference is required for the non-static field, method, or property '{0}'";
	public const string AmbigCall = "The call is ambiguous between the following methods or properties: '{0}' and '{1}'";
	public const string BadAccess = "'{0}' is inaccessible due to its protection level";
	public const string MethDelegateMismatch = "No overload for '{0}' matches delegate '{1}'";
	public const string AssgLvalueExpected = "The left-hand side of an assignment must be a variable, property or indexer";
	public const string NoConstructors = "The type '{0}' has no constructors defined";
	public const string BadDelegateConstructor = "The delegate '{0}' does not have a valid constructor";
	public const string PropertyLacksGet = "The property or indexer '{0}' cannot be used in this context because it lacks the get accessor";
	public const string ObjectProhibited = "Member '{0}' cannot be accessed with an instance reference; qualify it with a type name instead";
	public const string AssgReadonly = "A readonly field cannot be assigned to (except in a constructor or a variable initializer)";
	public const string RefReadonly = "A readonly field cannot be passed ref or out (except in a constructor)";
	public const string AssgReadonlyStatic = "A static readonly field cannot be assigned to (except in a static constructor or a variable initializer)";
	public const string RefReadonlyStatic = "A static readonly field cannot be passed ref or out (except in a static constructor)";
	public const string AssgReadonlyProp = "Property or indexer '{0}' cannot be assigned to -- it is read only";
	public const string AbstractBaseCall = "Cannot call an abstract base member: '{0}'";
	public const string RefProperty = "A property or indexer may not be passed as an out or ref parameter";
	public const string UnsafeNeeded = "Dynamic calls cannot be used in conjunction with pointers";
	public const string BadBoolOp = "In order to be applicable as a short circuit operator a user-defined logical operator ('{0}') must have the same return type as the type of its 2 parameters";
	public const string MustHaveOpTF = "The type ('{0}') must contain declarations of operator true and operator false";
	public const string ConstOutOfRangeChecked = "Constant value '{0}' cannot be converted to a '{1}' (use 'unchecked' syntax to override)";
	public const string AmbigMember = "Ambiguity between '{0}' and '{1}'";
	public const string SizeofUnsafe = "'{0}' does not have a predefined size, therefore sizeof can only be used in an unsafe context (consider using System.Runtime.InteropServices.Marshal.SizeOf)";
	public const string CallingFinalizeDepracated = "Destructors and object.Finalize cannot be called directly. Consider calling IDisposable.Dispose if available.";
	public const string CallingBaseFinalizeDeprecated = "Do not directly call your base class Finalize method. It is called automatically from your destructor.";
	public const string NoImplicitConvCast = "Cannot implicitly convert type '{0}' to '{1}'. An explicit conversion exists (are you missing a cast?)";
	public const string InaccessibleGetter = "The property or indexer '{0}' cannot be used in this context because the get accessor is inaccessible";
	public const string InaccessibleSetter = "The property or indexer '{0}' cannot be used in this context because the set accessor is inaccessible";
	public const string BadArity = "Using the generic {1} '{0}' requires '{2}' type arguments";
	public const string BadTypeArgument = "The type '{0}' may not be used as a type argument";
	public const string TypeArgsNotAllowed = "The {1} '{0}' cannot be used with type arguments";
	public const string HasNoTypeVars = "The non-generic {1} '{0}' cannot be used with type arguments";
	public const string NewConstraintNotSatisfied = "'{2}' must be a non-abstract type with a public parameterless constructor in order to use it as parameter '{1}' in the generic type or method '{0}'";
	public const string GenericConstraintNotSatisfiedRefType = "The type '{3}' cannot be used as type parameter '{2}' in the generic type or method '{0}'. There is no implicit reference conversion from '{3}' to '{1}'.";
	public const string GenericConstraintNotSatisfiedNullableEnum = "The type '{3}' cannot be used as type parameter '{2}' in the generic type or method '{0}'. The nullable type '{3}' does not satisfy the constraint of '{1}'.";
	public const string GenericConstraintNotSatisfiedNullableInterface = "The type '{3}' cannot be used as type parameter '{2}' in the generic type or method '{0}'. The nullable type '{3}' does not satisfy the constraint of '{1}'. Nullable types can not satisfy any interface constraints.";
	public const string GenericConstraintNotSatisfiedTyVar = "The type '{3}' cannot be used as type parameter '{2}' in the generic type or method '{0}'. There is no boxing conversion or type parameter conversion from '{3}' to '{1}'.";
	public const string GenericConstraintNotSatisfiedValType = "The type '{3}' cannot be used as type parameter '{2}' in the generic type or method '{0}'. There is no boxing conversion from '{3}' to '{1}'.";
	public const string TypeVarCantBeNull = "Cannot convert null to type parameter '{0}' because it could be a non-nullable value type. Consider using 'default({0})' instead.";
	public const string BadRetType = "'{1} {0}' has the wrong return type";
	public const string CantInferMethTypeArgs = "The type arguments for method '{0}' cannot be inferred from the usage. Try specifying the type arguments explicitly.";
	public const string MethGrpToNonDel = "Cannot convert method group '{0}' to non-delegate type '{1}'. Did you intend to invoke the method?";
	public const string RefConstraintNotSatisfied = "The type '{2}' must be a reference type in order to use it as parameter '{1}' in the generic type or method '{0}'";
	public const string ValConstraintNotSatisfied = "The type '{2}' must be a non-nullable value type in order to use it as parameter '{1}' in the generic type or method '{0}'";
	public const string AmbigUDConv = "Ambiguous user defined conversions '{0}' and '{1}' when converting from '{2}' to '{3}'";
	public const string PredefinedTypeNotFound = "Predefined type '{0}' is not defined or imported";
	public const string BindToBogus = "'{0}' is not supported by the language";
	public const string CantCallSpecialMethod = "'{0}': cannot explicitly call operator or accessor";
	public const string BogusType = "'{0}' is a type not supported by the language";
	public const string MissingPredefinedMember = "Missing compiler required member '{0}.{1}'";
	public const string LiteralDoubleCast = "Literal of type double cannot be implicitly converted to type '{1}'; use an '{0}' suffix to create a literal of this type";
	public const string ConvertToStaticClass = "Cannot convert to static type '{0}'";
	public const string GenericArgIsStaticClass = "'{0}': static types cannot be used as type arguments";
	public const string PartialMethodToDelegate = "Cannot create delegate from method '{0}' because it is a partial method without an implementing declaration";
	public const string IncrementLvalueExpected = "The operand of an increment or decrement operator must be a variable, property or indexer";
	public const string NoSuchMemberOrExtension = "'{0}' does not contain a definition for '{1}' and no extension method '{1}' accepting a first argument of type '{0}' could be found (are you missing a using directive or an assembly reference?)";
	public const string ValueTypeExtDelegate = "Extension methods '{0}' defined on value type '{1}' cannot be used to create delegates";
	public const string BadArgCount = "No overload for method '{0}' takes '{1}' arguments";
	public const string BadArgTypes = "The best overloaded method match for '{0}' has some invalid arguments";
	public const string BadArgType = "Argument '{0}': cannot convert from '{1}' to '{2}'";
	public const string RefLvalueExpected = "A ref or out argument must be an assignable variable";
	public const string BadProtectedAccess = "Cannot access protected member '{0}' via a qualifier of type '{1}'; the qualifier must be of type '{2}' (or derived from it)";
	public const string BindToBogusProp2 = "Property, indexer, or event '{0}' is not supported by the language; try directly calling accessor methods '{1}' or '{2}'";
	public const string BindToBogusProp1 = "Property, indexer, or event '{0}' is not supported by the language; try directly calling accessor method '{1}'";
	public const string BadDelArgCount = "Delegate '{0}' does not take '{1}' arguments";
	public const string BadDelArgTypes = "Delegate '{0}' has some invalid arguments";
	public const string AssgReadonlyLocal = "Cannot assign to '{0}' because it is read-only";
	public const string RefReadonlyLocal = "Cannot pass '{0}' as a ref or out argument because it is read-only";
	public const string ReturnNotLValue = "Cannot modify the return value of '{0}' because it is not a variable";
	public const string BadArgExtraRef = "Argument '{0}' should not be passed with the '{1}' keyword";
	public const string BadArgRef = "Argument '{0}' must be passed with the '{1}' keyword";
	public const string AssgReadonly2 = "Members of readonly field '{0}' cannot be modified (except in a constructor or a variable initializer)";
	public const string RefReadonly2 = "Members of readonly field '{0}' cannot be passed ref or out (except in a constructor)";
	public const string AssgReadonlyStatic2 = "Fields of static readonly field '{0}' cannot be assigned to (except in a static constructor or a variable initializer)";
	public const string RefReadonlyStatic2 = "Fields of static readonly field '{0}' cannot be passed ref or out (except in a static constructor)";
	public const string AssgReadonlyLocalCause = "Cannot assign to '{0}' because it is a '{1}'";
	public const string RefReadonlyLocalCause = "Cannot pass '{0}' as a ref or out argument because it is a '{1}'";
	public const string DelegateOnNullable = "Cannot bind delegate to '{0}' because it is a member of 'System.Nullable<T>'";
	public const string BadCtorArgCount = "'{0}' does not contain a constructor that takes '{1}' arguments";
	public const string BadExtensionArgTypes = "'{0}' does not contain a definition for '{1}' and the best extension method overload '{2}' has some invalid arguments";
	public const string BadInstanceArgType = "Instance argument: cannot convert from '{0}' to '{1}'";
	public const string BadArgTypesForCollectionAdd = "The best overloaded Add method '{0}' for the collection initializer has some invalid arguments";
	public const string InitializerAddHasParamModifiers = "The best overloaded method match '{0}' for the collection initializer element cannot be used. Collection initializer 'Add' methods cannot have ref or out parameters.";
	public const string NonInvocableMemberCalled = "Non-invocable member '{0}' cannot be used like a method.";
	public const string NamedArgumentSpecificationBeforeFixedArgument = "Named argument specifications must appear after all fixed arguments have been specified";
	public const string BadNamedArgument = "The best overload for '{0}' does not have a parameter named '{1}'";
	public const string BadNamedArgumentForDelegateInvoke = "The delegate '{0}' does not have a parameter named '{1}'";
	public const string DuplicateNamedArgument = "Named argument '{0}' cannot be specified multiple times";
	public const string NamedArgumentUsedInPositional = "Named argument '{0}' specifies a parameter for which a positional argument has already been given";
	public const string TypeArgumentRequiredForStaticCall = "The first argument to dynamically-bound static call must be a Type";
}
