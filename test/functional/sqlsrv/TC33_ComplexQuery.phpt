--TEST--
Complex Query Test
--DESCRIPTION--
Verifies the behavior of INSERT queries with and without the IDENTITY flag set.
--ENV--
PHPT_EXEC=true
--SKIPIF--
<?php require('skipif_versions_old.inc'); ?>
--FILE--
<?php

require_once('MsCommon.inc');

function complexQuery()
{
    $testName = "Statement - Complex Query";
    startTest($testName);

    setup();
    $conn1 = AE\connect();

    $tableName = 'TC33test';
    $columns = array(new AE\ColumnMeta('int', 'c1_int', "IDENTITY"),
                     new AE\ColumnMeta('tinyint', 'c2_tinyint'),
                     new AE\ColumnMeta('smallint', 'c3_smallint'),
                     new AE\ColumnMeta('bigint', 'c4_bigint'),
                     new AE\ColumnMeta('varchar(512)', 'c5_varchar'));
    AE\createTable($conn1, $tableName, $columns);

    // SET IDENTITY_INSERT ON/OFF only works at execute or run time and not at parse time
    // because a prepared statement runs in a separate context
    // https://technet.microsoft.com/en-us/library/ms188059(v=sql.110).aspx
    $query = "SET IDENTITY_INSERT [$tableName] ON;";
    $stmt = sqlsrv_query($conn1, $query);
    if (!$stmt) {
        die("Unexpected execution outcome for \'$query\'.");
    }
    
    // expect this to pass
    $inputs = array("c1_int" => -204401468, "c2_tinyint" => 168, "c3_smallint" => 4787, "c4_bigint" =>1583186637, "c5_varchar" => "�<��C~z��a.Oa._ߣ*�<u_��C�oa �����a+O�h�obUa:zB_C�@~U�z+���//Z@�o_:r,o��r�zoZ�*ߪ��~ U�a>��ZU�/�_Z����h�C�+/.ob�|���,���:��:*/>+/�a�.��<�:>�O~*~��z�<����.O,>�,�b�@b�h�C*<<hb��*o��h���a+A/_@b/�B�B��@�~z�Z�C@�U_�U�hvU*a@���:�ZA�Ab�U_��b��:���or��ߪ_��֪z����oa� <�~zZ�aB.+�A���><�:/Ur �U��Oa�:a|++��.r~:/+�|��o++v_@BZ:��A�C�.�/Ab<,��>U����bb|��ߣ:�<<b��a+,<_a�._�>�<|�z�z@>��:a,C�r__�.<��C�+U�U�_�z� b�~�o|, .�,b/U>��aBZ@ܣ: b�v�b>�/��@��/�b�+r:Z�>��|�u��ZAC:C�h *.ã�_��u|Ur�.:aAUv@u>@<��.<�Z b�ZA�֣o���*,�:��");
    $stmt = insertTest($conn1, $tableName, true, $inputs);

    $query = "SET IDENTITY_INSERT [$tableName] OFF;";
    $stmt = sqlsrv_query($conn1, $query);
    if (!$stmt) {
        die("Unexpected execution outcome for \'$query\'.");
    }

    // expect this to fail
    $inputs = array("c1_int" => 1264768176, "c2_tinyint" => 111, "c3_smallint" => 23449, "c4_bigint" =>1421472052, "c5_varchar" => "u�C@b�UOv~,�v,BZ�*oh>zb_���<@*OO�_�<�u�/o�r <��b�U�����~�~� bܩ�.u�Т�:|_���B�b����v@,<C�O�v~:+,CZ�vhC/o�Uu��a<�>�/Ub,+AЩ�:�r�B+~~�����+_<vO@ �����aCz���@:r�.~vh~r.�b���_�C�r B��:BbUv���Z+|,C�aA�C,a�bb*U���A hCu�hOb �|�C.<C<.aB�vu���,A�a>AB��U/O<����O�uߣ~u�+��rb�/:��o  /_�O:u�z�Uv�A�_B�/>UCr,�� a��aãv�Z@�r*_::~/+.�~�a��bz*z<~rU~O+Z|A<_B�ߩ�� ::.�b���r/�rh�:��U �OA~A�r<��v��+hC/v�oU�+O��*�B�.Zbh/�,��>*���U��>a�bBbv���/b�|�� u.z��~��z�U.UA*a*.�>� r� ~C��a�+r�~�@a�/�C�*a,��bb<o+v.�u<�B<�BZ��u�/_>*~");
    $stmt = insertTest($conn1, $tableName, false, $inputs);

    // expect this to pass
    $query = "SET IDENTITY_INSERT [$tableName] ON; SQL; SET IDENTITY_INSERT [$tableName] OFF;";
    if (AE\isColEncrypted()){
        // When AE is enabled, SQL types must be specified for sqlsrv_query
        $inputs = array("c1_int" => array(-411114769, null, null, SQLSRV_SQLTYPE_INT), 
                        "c2_tinyint" => array(198, null, null, SQLSRV_SQLTYPE_TINYINT),
                        "c3_smallint" => array(1378, null, null, SQLSRV_SQLTYPE_SMALLINT),
                        "c4_bigint" => array(140345831, null, null, SQLSRV_SQLTYPE_BIGINT), 
                        "c5_varchar" => array("�@�a�rêA*���A>_hO�v@|h~O<�+*��Cbaz�a�Z/��:��u��az��Ah+u+r�:| U*������_v�@@~Ch��_�*AA�B��B,�b��.�B+u*CAv�,�>��CU<���rz�@�r�*�ub�B�a�@�.�Bv�o~ ��o o�u/>���,�,�aO��>�C:�Z>���<�+�r.bO.�,uA�r><ov:,�����+�./||CU��_�Īh~<�_�/hb� ĩuBu�<�@bo��B�C�A/��:� �U�*�vu�.B���o_�b�r_��>��ܣB�A�va�v��C�U�  �v�u�><��UC*a�U�r�hr+>|���|o�r�У<�<�|A�oh�A�_vu~:~��h�+�Bu�� �@Z+�@h��|@bU�_�/� |:�zb>@Uߩ  ��o �@��B�_�BOB��hC�b~�>�� r���Uzu�rbz�/��U��u�.�@�__vBb�/�r��u�z��*�/*�O", null, null, SQLSRV_SQLTYPE_VARCHAR(512)));
        $stmt = insertTest($conn1, $tableName, true, $inputs, $query);
    } else {
        $inputs = array("c1_int" => -411114769, "c2_tinyint" => 198, "c3_smallint" => 1378, "c4_bigint" => 140345831, "c5_varchar" => "�@�a�rêA*���A>_hO�v@|h~O<�+*��Cbaz�a�Z/��:��u��az��Ah+u+r�:| U*������_v�@@~Ch��_�*AA�B��B,�b��.�B+u*CAv�,�>��CU<���rz�@�r�*�ub�B�a�@�.�Bv�o~ ��o o�u/>���,�,�aO��>�C:�Z>���<�+�r.bO.�,uA�r><ov:,�����+�./||CU��_�Īh~<�_�/hb� ĩuBu�<�@bo��B�C�A/��:� �U�*�vu�.B���o_�b�r_��>��ܣB�A�va�v��C�U�  �v�u�><��UC*a�U�r�hr+>|���|o�r�У<�<�|A�oh�A�_vu~:~��h�+�Bu�� �@Z+�@h��|@bU�_�/� |:�zb>@Uߩ  ��o �@��B�_�BOB��hC�b~�>�� r���Uzu�rbz�/��U��u�.�@�__vBb�/�r��u�z��*�/*�O");
        $stmt = insertTest($conn1, $tableName, true, $inputs, $query);
    }

    $stmt1 = selectFromTable($conn1, $tableName);
    $rowCount = rowCount($stmt1);
    sqlsrv_free_stmt($stmt1);

    if ($rowCount != 2) {
        die("Table $tableName has $rowCount rows instead of 2.");
    }

    dropTable($conn1, $tableName);

    sqlsrv_close($conn1);

    endTest($testName);
}

function insertTest($conn, $tableName, $expectedOutcome, $inputs, $query = null)
{
    $stmt = null;
    if (!AE\isColEncrypted()) {
        $insertSql = AE\getInsertSqlComplete($tableName, $inputs);
        if (! is_null($query)) {
            $sql = str_replace("SQL", $insertSql, $query);
        } else {
            $sql = $insertSql;
        }
        $stmt = sqlsrv_query($conn, $sql);
        $actualOutcome = ($stmt !== false);
    } else {
        // must bind parameters
        $insertSql = AE\getInsertSqlPlaceholders($tableName, $inputs);
        $params = array();
        foreach ($inputs as $key => $input) {
            array_push($params, $inputs[$key]);
        }
        if (! is_null($query)) {
            // this contains a batch of sql statements, 
            // with set identity_insert on or off 
            // thus, sqlsrv_query should be called
            $sql = str_replace("SQL", $insertSql, $query);
            $stmt = sqlsrv_query($conn, $sql, $params);
            $actualOutcome = ($stmt !== false);
        } else {
            // just a regular insert, so use sqlsrv_prepare
            $sql = $insertSql;
            $actualOutcome = true;
            $stmt = sqlsrv_prepare($conn, $sql, $params);
            if ($stmt) {
                $result = sqlsrv_execute($stmt);
                $actualOutcome = ($result !== false);
            }
        }
    }
    if ($actualOutcome != $expectedOutcome) {
        die("Unexpected execution outcome for \'$sql\'.");
    }
}

try {
    complexQuery();
} catch (Exception $e) {
    echo $e->getMessage();
}

?>
--EXPECT--
Test "Statement - Complex Query" completed successfully.
